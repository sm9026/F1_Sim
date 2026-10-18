#include "f1_sim/data_access/data_access_layer.hpp"
#include "json_parser.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace f1_sim {
namespace data_access {

namespace {

using detail::JsonValue;

constexpr const char* kSupportedSchemaVersion = "1.0.0";

bool isValidIsoDate(const std::string& s) noexcept {
    if (s.size() != 10) {
        return false;
    }
    for (std::size_t i = 0; i < 10; ++i) {
        if (i == 4 || i == 7) {
            if (s[i] != '-') return false;
        } else {
            if (s[i] < '0' || s[i] > '9') return false;
        }
    }
    int month = (s[5] - '0') * 10 + (s[6] - '0');
    int day = (s[8] - '0') * 10 + (s[9] - '0');
    return (month >= 1 && month <= 12 && day >= 1 && day <= 31);
}

bool isValidTrackOrEventSlug(const std::string& s) noexcept {
    if (s.empty() || s.size() > 64) {
        return false;
    }
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '_');
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::optional<SourceStatus> parseSourceStatus(const std::string& s) noexcept {
    if (s == "PUBLISHED_OBSERVED")  return SourceStatus::PublishedObserved;
    if (s == "DERIVED")             return SourceStatus::Derived;
    if (s == "CALIBRATED")          return SourceStatus::Calibrated;
    if (s == "ESTIMATED")           return SourceStatus::Estimated;
    if (s == "ASSUMED")             return SourceStatus::Assumed;
    if (s == "UNKNOWN_UNAVAILABLE") return SourceStatus::UnknownUnavailable;
    return std::nullopt;
}

std::optional<ConfidenceTier> parseConfidenceTier(const std::string& s) noexcept {
    if (s == "HIGH")        return ConfidenceTier::High;
    if (s == "MEDIUM")      return ConfidenceTier::Medium;
    if (s == "LOW")         return ConfidenceTier::Low;
    if (s == "PROVISIONAL") return ConfidenceTier::Provisional;
    return std::nullopt;
}

std::optional<MissingPolicy> parseMissingPolicy(const std::string& s) noexcept {
    if (s == "REJECT_SIMULATION")      return MissingPolicy::RejectSimulation;
    if (s == "USE_DECLARED_DEFAULT")   return MissingPolicy::UseDeclaredDefault;
    if (s == "INTERPOLATE_BOUNDED")    return MissingPolicy::InterpolateBounded;
    if (s == "DEGRADE_TO_LOWER_STAGE") return MissingPolicy::DegradeToLowerStage;
    return std::nullopt;
}

std::optional<RaceDirection> parseRaceDirection(const std::string& s) noexcept {
    if (s == "CLOCKWISE")     return RaceDirection::Clockwise;
    if (s == "ANTICLOCKWISE") return RaceDirection::Anticlockwise;
    if (s == "FIGURE_EIGHT")  return RaceDirection::FigureEight;
    return std::nullopt;
}

std::optional<CompetitionFormat> parseCompetitionFormat(const std::string& s) noexcept {
    if (s == "STANDARD")                  return CompetitionFormat::Standard;
    if (s == "ALTERNATIVE_FORMAT_SPRINT") return CompetitionFormat::AlternativeFormatSprint;
    return std::nullopt;
}

std::optional<TyreRole> parseTyreRole(const std::string& s) noexcept {
    if (s == "HARD" || s == "H")   return TyreRole::Hard;
    if (s == "MEDIUM" || s == "M") return TyreRole::Medium;
    if (s == "SOFT" || s == "S")   return TyreRole::Soft;
    return std::nullopt;
}

std::optional<PhysicalCompoundId> parsePhysicalCompoundId(const std::string& s) noexcept {
    if (s == "C1") return PhysicalCompoundId::C1;
    if (s == "C2") return PhysicalCompoundId::C2;
    if (s == "C3") return PhysicalCompoundId::C3;
    if (s == "C4") return PhysicalCompoundId::C4;
    if (s == "C5") return PhysicalCompoundId::C5;
    return std::nullopt;
}

struct ReadAndParseOutcome {
    LoadStatus status = LoadStatus::Success;
    std::optional<JsonValue> root;
    std::vector<Diagnostic> diagnostics;
};

ReadAndParseOutcome readAndParseJsonFile(
    const ResolvedDatasetPath& resolved,
    const std::string& dataset_id) {
    ReadAndParseOutcome out;
    std::error_code ec;

    if (!std::filesystem::exists(resolved.absolute_path, ec) || ec) {
        out.status = LoadStatus::FileNotFound;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::DatasetFileNotFound,
            "Required dataset file does not exist under configured data_root.",
            dataset_id,
            resolved.relative_path,
            "",
            "Existing regular file",
            "missing"
        });
        return out;
    }

    if (!std::filesystem::is_regular_file(resolved.absolute_path, ec) || ec) {
        out.status = LoadStatus::IoError;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::FileReadPermissionError,
            "Dataset target is not a regular readable file.",
            dataset_id,
            resolved.relative_path,
            "",
            "Regular file",
            "non-regular file or directory"
        });
        return out;
    }

    std::ifstream ifs(resolved.absolute_path, std::ios::in | std::ios::binary);
    if (!ifs.is_open() || !ifs.good()) {
        out.status = LoadStatus::IoError;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::FileReadPermissionError,
            "Unable to open dataset file for reading (check file permissions).",
            dataset_id,
            resolved.relative_path,
            "",
            "Readable file stream",
            "open failed"
        });
        return out;
    }

    std::ostringstream buffer;
    buffer << ifs.rdbuf();
    if (ifs.bad()) {
        out.status = LoadStatus::IoError;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::FileReadPermissionError,
            "I/O failure while reading dataset file contents.",
            dataset_id,
            resolved.relative_path,
            "",
            "Clean EOF read",
            "stream badbit set"
        });
        return out;
    }

    auto parsed = detail::parseJsonDocument(buffer.str());
    if (!parsed.ok()) {
        out.status = LoadStatus::MalformedJson;
        const auto& err = *parsed.error;
        std::ostringstream msg;
        msg << "Malformed JSON at line " << err.line << ", column " << err.column
            << ": " << err.message;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::MalformedJson,
            msg.str(),
            dataset_id,
            resolved.relative_path,
            "",
            "Syntactically valid RFC 8259 JSON",
            "line " + std::to_string(err.line) + ", col " + std::to_string(err.column)
        });
        return out;
    }

    if (!parsed.value->isObject()) {
        out.status = LoadStatus::SchemaValidationFailed;
        out.diagnostics.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::StructuralSchemaFailure,
            "Root JSON value must be a JSON object.",
            dataset_id,
            resolved.relative_path,
            "/",
            "object",
            parsed.value->typeName()
        });
        return out;
    }

    out.root = std::move(*parsed.value);
    return out;
}

/**
 * @brief Helper context for extracting and validating fields from a JSON object.
 */
struct ValidationContext {
    std::string dataset_id;
    std::string relative_path;
    LoadStatus failure_status = LoadStatus::Success;
    std::vector<Diagnostic> diagnostics;

    ValidationContext(std::string ds_id, std::string rel_path)
        : dataset_id(std::move(ds_id)), relative_path(std::move(rel_path)) {}

    void recordFailure(
        LoadStatus status,
        DiagnosticCode code,
        const std::string& field_path,
        const std::string& message,
        const std::string& expected = "",
        const std::string& actual = "") {
        auto stageRank = [](LoadStatus s) -> int {
            switch (s) {
                case LoadStatus::UnsupportedSchemaVersion: return 1;
                case LoadStatus::SchemaValidationFailed:   return 2;
                case LoadStatus::SemanticValidationFailed: return 3;
                case LoadStatus::CrossReferenceFailed:     return 4;
                default:                                   return 10;
            }
        };
        if (failure_status == LoadStatus::Success || stageRank(status) < stageRank(failure_status)) {
            failure_status = status;
        }
        diagnostics.push_back(Diagnostic{
            Severity::Error,
            code,
            message,
            dataset_id,
            relative_path,
            field_path,
            expected,
            actual
        });
    }

    void recordWarning(
        DiagnosticCode code,
        const std::string& field_path,
        const std::string& message,
        const std::string& expected = "",
        const std::string& actual = "") {
        diagnostics.push_back(Diagnostic{
            Severity::Warning,
            code,
            message,
            dataset_id,
            relative_path,
            field_path,
            expected,
            actual
        });
    }

    bool hasFailed() const noexcept {
        return failure_status != LoadStatus::Success;
    }
};

bool validateSchemaHeader(
    const JsonValue& obj,
    const std::string& expected_schema_name,
    ValidationContext& ctx,
    std::string& out_schema_name,
    std::string& out_schema_version) {
    const JsonValue* sname = obj.findKey("schema_name");
    if (sname == nullptr || !sname->isString()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/schema_name",
            "Missing or non-string required field 'schema_name'.",
            expected_schema_name,
            sname ? sname->typeName() : "missing");
        return false;
    }
    out_schema_name = sname->asString();
    if (out_schema_name != expected_schema_name) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/schema_name",
            "Unexpected schema_name in dataset file.",
            expected_schema_name,
            out_schema_name);
        return false;
    }

    const JsonValue* sver = obj.findKey("schema_version");
    if (sver == nullptr || !sver->isString()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/schema_version",
            "Missing or non-string required field 'schema_version'.",
            kSupportedSchemaVersion,
            sver ? sver->typeName() : "missing");
        return false;
    }
    out_schema_version = sver->asString();
    if (out_schema_version != kSupportedSchemaVersion) {
        ctx.recordFailure(
            LoadStatus::UnsupportedSchemaVersion,
            DiagnosticCode::UnsupportedSchemaVersion,
            "/schema_version",
            "Unsupported schema_version '" + out_schema_version + "'.",
            kSupportedSchemaVersion,
            out_schema_version);
        return false;
    }
    return true;
}

bool requireNonEmptyString(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    ValidationContext& ctx,
    std::string& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Missing required string field '" + key + "'.",
            "non-empty string",
            "missing");
        return false;
    }
    if (!v->isString()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be a string (null or non-string is not allowed).",
            "string",
            v->typeName());
        return false;
    }
    if (v->asString().empty()) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            json_ptr,
            "Required string field '" + key + "' must not be empty.",
            "non-empty string",
            "\"\"");
        return false;
    }
    out_val = v->asString();
    return true;
}

bool readOptionalString(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    bool require_key_present,
    ValidationContext& ctx,
    std::optional<std::string>& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        if (require_key_present) {
            ctx.recordFailure(
                LoadStatus::SchemaValidationFailed,
                DiagnosticCode::StructuralSchemaFailure,
                json_ptr,
                "Missing required key '" + key + "' (must be string or explicit null).",
                "string or null",
                "missing");
            return false;
        }
        out_val = std::nullopt;
        return true;
    }
    if (v->isNull()) {
        out_val = std::nullopt;
        return true;
    }
    if (!v->isString()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be a string or null.",
            "string or null",
            v->typeName());
        return false;
    }
    out_val = v->asString();
    return true;
}

bool requireInt(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    ValidationContext& ctx,
    int& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Missing required integer field '" + key + "'.",
            "integer",
            "missing");
        return false;
    }
    if (!v->isInteger()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be an integer.",
            "integer",
            v->typeName());
        return false;
    }
    out_val = static_cast<int>(v->asInteger());
    return true;
}

bool readOptionalInt(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    bool require_key_present,
    ValidationContext& ctx,
    std::optional<int>& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        if (require_key_present) {
            ctx.recordFailure(
                LoadStatus::SchemaValidationFailed,
                DiagnosticCode::StructuralSchemaFailure,
                json_ptr,
                "Missing required key '" + key + "' (must be integer or explicit null).",
                "integer or null",
                "missing");
            return false;
        }
        out_val = std::nullopt;
        return true;
    }
    if (v->isNull()) {
        out_val = std::nullopt;
        return true;
    }
    if (!v->isInteger()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be an integer or null.",
            "integer or null",
            v->typeName());
        return false;
    }
    out_val = static_cast<int>(v->asInteger());
    return true;
}

bool requireDouble(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    ValidationContext& ctx,
    double& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Missing required numeric field '" + key + "'.",
            "number",
            "missing");
        return false;
    }
    if (!v->isNumber()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be a number (null is not allowed for this field).",
            "number",
            v->typeName());
        return false;
    }
    out_val = v->asNumber();
    return true;
}

bool readNullableDouble(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    bool require_key_present,
    bool warn_on_null,
    ValidationContext& ctx,
    std::optional<double>& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr) {
        if (require_key_present) {
            ctx.recordFailure(
                LoadStatus::SchemaValidationFailed,
                DiagnosticCode::StructuralSchemaFailure,
                json_ptr,
                "Missing required key '" + key + "' (must be number or explicit null).",
                "number or null",
                "missing");
            return false;
        }
        out_val = std::nullopt;
        return true;
    }
    if (v->isNull()) {
        out_val = std::nullopt;
        if (warn_on_null) {
            ctx.recordWarning(
                DiagnosticCode::LegitimateUnknownValue,
                json_ptr,
                "Field '" + key + "' is explicitly null (legitimately unknown or uncalibrated); preserved as std::nullopt.",
                "calibrated/measured number",
                "null");
        }
        return true;
    }
    if (!v->isNumber()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be a number or null.",
            "number or null",
            v->typeName());
        return false;
    }
    out_val = v->asNumber();
    return true;
}

bool requireBool(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    ValidationContext& ctx,
    bool& out_val) {
    const JsonValue* v = obj.findKey(key);
    if (v == nullptr || !v->isBool()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Missing or non-boolean required field '" + key + "'.",
            "boolean",
            v ? v->typeName() : "missing");
        return false;
    }
    out_val = v->asBool();
    return true;
}

void checkRangeDouble(
    double val,
    double min_val,
    double max_val,
    const std::string& json_ptr,
    const std::string& field_name,
    ValidationContext& ctx) {
    if (val < min_val || val > max_val) {
        std::ostringstream exp;
        exp << "[" << min_val << ", " << max_val << "]";
        std::ostringstream act;
        act << val;
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            json_ptr,
            "Numeric field '" + field_name + "' is outside its legal range.",
            exp.str(),
            act.str());
    }
}

void checkRangeInt(
    int val,
    int min_val,
    int max_val,
    const std::string& json_ptr,
    const std::string& field_name,
    ValidationContext& ctx) {
    if (val < min_val || val > max_val) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            json_ptr,
            "Integer field '" + field_name + "' is outside its legal range.",
            "[" + std::to_string(min_val) + ", " + std::to_string(max_val) + "]",
            std::to_string(val));
    }
}

bool parseProvenanceObject(
    const JsonValue& prov_obj,
    const std::string& base_ptr,
    ValidationContext& ctx,
    ProvenanceMetadata& out_meta) {
    if (!prov_obj.isObject()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            base_ptr,
            "Provenance entry must be a JSON object.",
            "object",
            prov_obj.typeName());
        return false;
    }

    readOptionalString(prov_obj, "source_id", base_ptr + "/source_id", false, ctx, out_meta.source_id);

    std::string status_str;
    if (requireNonEmptyString(prov_obj, "source_status", base_ptr + "/source_status", ctx, status_str)) {
        auto st = parseSourceStatus(status_str);
        if (!st.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                base_ptr + "/source_status",
                "Invalid source_status enum value '" + status_str + "'.",
                "PUBLISHED_OBSERVED|DERIVED|CALIBRATED|ESTIMATED|ASSUMED|UNKNOWN_UNAVAILABLE",
                status_str);
        } else {
            out_meta.source_status = *st;
        }
    }

    requireNonEmptyString(prov_obj, "source_name", base_ptr + "/source_name", ctx, out_meta.source_name);
    requireNonEmptyString(prov_obj, "source_ref_uri", base_ptr + "/source_ref_uri", ctx, out_meta.source_ref_uri);
    requireNonEmptyString(prov_obj, "retrieved_at_utc", base_ptr + "/retrieved_at_utc", ctx, out_meta.retrieved_at_utc);
    if (requireNonEmptyString(prov_obj, "valid_from_date", base_ptr + "/valid_from_date", ctx, out_meta.valid_from_date)) {
        if (!isValidIsoDate(out_meta.valid_from_date)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                base_ptr + "/valid_from_date",
                "valid_from_date must be an ISO-8601 YYYY-MM-DD date.",
                "YYYY-MM-DD",
                out_meta.valid_from_date);
        }
    }

    readOptionalString(prov_obj, "valid_to_date", base_ptr + "/valid_to_date", false, ctx, out_meta.valid_to_date);
    if (out_meta.valid_to_date.has_value() && !isValidIsoDate(*out_meta.valid_to_date)) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            base_ptr + "/valid_to_date",
            "valid_to_date must be an ISO-8601 YYYY-MM-DD date or null.",
            "YYYY-MM-DD or null",
            *out_meta.valid_to_date);
    }

    readOptionalString(prov_obj, "derivation_method", base_ptr + "/derivation_method", false, ctx, out_meta.derivation_method);
    if ((out_meta.source_status == SourceStatus::Derived ||
         out_meta.source_status == SourceStatus::Calibrated ||
         out_meta.source_status == SourceStatus::Estimated) &&
        (!out_meta.derivation_method.has_value() || out_meta.derivation_method->empty())) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            base_ptr + "/derivation_method",
            "derivation_method is required when source_status is DERIVED, CALIBRATED, or ESTIMATED.",
            "non-empty derivation_method string",
            "null or empty");
    }

    readNullableDouble(prov_obj, "uncertainty_std", base_ptr + "/uncertainty_std", false, false, ctx, out_meta.uncertainty_std);
    if (out_meta.uncertainty_std.has_value() && *out_meta.uncertainty_std < 0.0) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            base_ptr + "/uncertainty_std",
            "uncertainty_std cannot be negative.",
            ">= 0.0",
            std::to_string(*out_meta.uncertainty_std));
    }

    readNullableDouble(prov_obj, "ci_lower_95", base_ptr + "/ci_lower_95", false, false, ctx, out_meta.ci_lower_95);
    readNullableDouble(prov_obj, "ci_upper_95", base_ptr + "/ci_upper_95", false, false, ctx, out_meta.ci_upper_95);
    if (out_meta.ci_lower_95.has_value() && out_meta.ci_upper_95.has_value() &&
        *out_meta.ci_lower_95 > *out_meta.ci_upper_95) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            base_ptr + "/ci_lower_95",
            "ci_lower_95 must not exceed ci_upper_95.",
            "ci_lower_95 <= ci_upper_95",
            std::to_string(*out_meta.ci_lower_95) + " > " + std::to_string(*out_meta.ci_upper_95));
    }

    std::string tier_str;
    if (requireNonEmptyString(prov_obj, "confidence_tier", base_ptr + "/confidence_tier", ctx, tier_str)) {
        auto tier = parseConfidenceTier(tier_str);
        if (!tier.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                base_ptr + "/confidence_tier",
                "Invalid confidence_tier enum value '" + tier_str + "'.",
                "HIGH|MEDIUM|LOW|PROVISIONAL",
                tier_str);
        } else {
            out_meta.confidence_tier = *tier;
        }
    }

    std::string policy_str;
    if (requireNonEmptyString(prov_obj, "missing_policy", base_ptr + "/missing_policy", ctx, policy_str)) {
        auto pol = parseMissingPolicy(policy_str);
        if (!pol.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                base_ptr + "/missing_policy",
                "Invalid missing_policy enum value '" + policy_str + "'.",
                "REJECT_SIMULATION|USE_DECLARED_DEFAULT|INTERPOLATE_BOUNDED|DEGRADE_TO_LOWER_STAGE",
                policy_str);
        } else {
            out_meta.missing_policy = *pol;
        }
    }

    if (out_meta.source_status == SourceStatus::Assumed ||
        out_meta.confidence_tier == ConfidenceTier::Low) {
        ctx.recordWarning(
            DiagnosticCode::AssumedOrLowConfidenceValue,
            base_ptr,
            "Provenance indicates ASSUMED status or LOW confidence tier (" + out_meta.source_name + ").",
            "HIGH/MEDIUM observed or calibrated",
            std::string(toString(out_meta.source_status)) + " / " + toString(out_meta.confidence_tier));
    }

    return !ctx.hasFailed();
}

void parseFieldProvenanceMap(
    const JsonValue& root_obj,
    const std::string& map_key,
    const std::string& base_ptr,
    ValidationContext& ctx,
    std::map<std::string, ProvenanceMetadata>& out_map) {
    const JsonValue* fp = root_obj.findKey(map_key);
    if (fp == nullptr || !fp->isObject() || fp->asObject().empty()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            base_ptr,
            "Required provenance map '" + map_key + "' must be a non-empty JSON object.",
            "non-empty object",
            fp ? fp->typeName() : "missing");
        return;
    }

    for (const auto& kv : fp->asObject()) {
        ProvenanceMetadata meta;
        parseProvenanceObject(kv.second, base_ptr + "/" + kv.first, ctx, meta);
        out_map.emplace(kv.first, std::move(meta));
    }
}

bool parseNominatedCompoundsTriplet(
    const JsonValue& obj,
    const std::string& key,
    const std::string& json_ptr,
    ValidationContext& ctx,
    std::array<PhysicalCompoundId, 3>& out_set) {
    const JsonValue* arr_val = obj.findKey(key);
    if (arr_val == nullptr || !arr_val->isArray()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            json_ptr,
            "Field '" + key + "' must be a JSON array of 3 physical compound IDs.",
            "array of 3 strings in {C1..C5}",
            arr_val ? arr_val->typeName() : "missing");
        return false;
    }
    const auto& arr = arr_val->asArray();
    if (arr.size() != 3) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            json_ptr,
            "Field '" + key + "' must contain exactly 3 nominated physical compounds.",
            "3 elements",
            std::to_string(arr.size()) + " elements");
        return false;
    }

    std::set<PhysicalCompoundId> distinct;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::string elem_ptr = json_ptr + "/" + std::to_string(i);
        if (!arr[i].isString()) {
            ctx.recordFailure(
                LoadStatus::SchemaValidationFailed,
                DiagnosticCode::StructuralSchemaFailure,
                elem_ptr,
                "Nominated compound entry must be a string in {C1..C5}.",
                "string",
                arr[i].typeName());
            return false;
        }
        auto cid = parsePhysicalCompoundId(arr[i].asString());
        if (!cid.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                elem_ptr,
                "Invalid physical compound identifier '" + arr[i].asString() + "'.",
                "C1|C2|C3|C4|C5",
                arr[i].asString());
            return false;
        }
        if (!distinct.insert(*cid).second) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                elem_ptr,
                "Duplicate compound '" + arr[i].asString() + "' in nominated_compounds_set.",
                "3 distinct compounds in {C1..C5}",
                arr[i].asString());
            return false;
        }
        out_set[i] = *cid;
    }
    return true;
}

} // namespace

DataAccessLayer::DataAccessLayer(
    std::filesystem::path data_root,
    std::shared_ptr<DiagnosticSink> diagnostic_sink)
    : resolver_(std::move(data_root)),
      diagnostic_sink_(std::move(diagnostic_sink)) {}

void DataAccessLayer::emitDiagnostics(const std::vector<Diagnostic>& diagnostics) const {
    if (!diagnostic_sink_) {
        return;
    }
    for (const auto& d : diagnostics) {
        diagnostic_sink_->record(d);
    }
}

Diagnostic DataAccessLayer::reportEngineDiscrepancy(const EngineDiscrepancyReport& report) const {
    Diagnostic diag{
        report.severity,
        report.code,
        report.message.empty() ? "Engine reported a runtime data discrepancy." : report.message,
        report.dataset_id,
        report.relative_path,
        report.field_path,
        report.expected,
        report.actual
    };
    if (diagnostic_sink_) {
        diagnostic_sink_->record(diag);
    }
    return diag;
}

Result<TrackLayoutVersion> DataAccessLayer::loadTrackLayout(
    const std::string& track_id,
    int layout_version) const {
    auto resolved = resolver_.resolveTrackLayout(track_id, layout_version);
    if (!resolved.ok()) {
        emitDiagnostics(resolved.diagnostics);
        return Result<TrackLayoutVersion>::failure(resolved.status, std::move(resolved.diagnostics));
    }

    const std::string dataset_id = track_id + "_layout_v" + std::to_string(layout_version);
    auto parsed = readAndParseJsonFile(*resolved.value, dataset_id);
    if (parsed.status != LoadStatus::Success) {
        emitDiagnostics(parsed.diagnostics);
        return Result<TrackLayoutVersion>::failure(parsed.status, std::move(parsed.diagnostics));
    }

    const JsonValue& obj = *parsed.root;
    ValidationContext ctx{dataset_id, resolved.value->relative_path};
    TrackLayoutVersion model;

    if (!validateSchemaHeader(obj, "TrackLayoutVersion", ctx, model.schema_name, model.schema_version)) {
        emitDiagnostics(ctx.diagnostics);
        return Result<TrackLayoutVersion>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }

    if (requireNonEmptyString(obj, "track_id", "/track_id", ctx, model.track_id)) {
        if (!isValidTrackOrEventSlug(model.track_id)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/track_id",
                "track_id must match pattern [a-z0-9_]+.",
                "[a-z0-9_]+",
                model.track_id);
        } else if (model.track_id != track_id) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/track_id",
                "Loaded TrackLayoutVersion track_id does not match requested track_id.",
                track_id,
                model.track_id);
        }
    }

    if (requireInt(obj, "layout_version", "/layout_version", ctx, model.layout_version)) {
        if (model.layout_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/layout_version",
                "layout_version must be >= 1.",
                ">= 1",
                std::to_string(model.layout_version));
        } else if (model.layout_version != layout_version) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/layout_version",
                "Loaded TrackLayoutVersion layout_version does not match requested layout_version.",
                std::to_string(layout_version),
                std::to_string(model.layout_version));
        }
    }

    requireNonEmptyString(obj, "track_name", "/track_name", ctx, model.track_name);
    requireNonEmptyString(obj, "country", "/country", ctx, model.country);
    requireNonEmptyString(obj, "locality", "/locality", ctx, model.locality);

    if (requireDouble(obj, "latitude_deg", "/latitude_deg", ctx, model.latitude_deg)) {
        checkRangeDouble(model.latitude_deg, -90.0, 90.0, "/latitude_deg", "latitude_deg", ctx);
    }
    if (requireDouble(obj, "longitude_deg", "/longitude_deg", ctx, model.longitude_deg)) {
        checkRangeDouble(model.longitude_deg, -180.0, 180.0, "/longitude_deg", "longitude_deg", ctx);
    }
    if (requireDouble(obj, "altitude_msl_m", "/altitude_msl_m", ctx, model.altitude_msl_m)) {
        checkRangeDouble(model.altitude_msl_m, -50.0, 2500.0, "/altitude_msl_m", "altitude_msl_m", ctx);
    }

    if (requireNonEmptyString(obj, "valid_from_date", "/valid_from_date", ctx, model.valid_from_date)) {
        if (!isValidIsoDate(model.valid_from_date)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/valid_from_date",
                "valid_from_date must be an ISO-8601 YYYY-MM-DD date.",
                "YYYY-MM-DD",
                model.valid_from_date);
        }
    }
    readOptionalString(obj, "valid_to_date", "/valid_to_date", true, ctx, model.valid_to_date);
    if (model.valid_to_date.has_value() && !isValidIsoDate(*model.valid_to_date)) {
        ctx.recordFailure(
            LoadStatus::SemanticValidationFailed,
            DiagnosticCode::InvalidRangeOrEnum,
            "/valid_to_date",
            "valid_to_date must be an ISO-8601 YYYY-MM-DD date or null.",
            "YYYY-MM-DD or null",
            *model.valid_to_date);
    }

    if (requireDouble(obj, "lap_length_m", "/lap_length_m", ctx, model.lap_length_m)) {
        checkRangeDouble(model.lap_length_m, 3000.0, 8000.0, "/lap_length_m", "lap_length_m", ctx);
    }

    readNullableDouble(obj, "sector1_length_m", "/sector1_length_m", true, true, ctx, model.sector1_length_m);
    readNullableDouble(obj, "sector2_length_m", "/sector2_length_m", true, true, ctx, model.sector2_length_m);
    readNullableDouble(obj, "sector3_length_m", "/sector3_length_m", true, true, ctx, model.sector3_length_m);

    if (model.hasCompleteSectorLengths()) {
        double sum_sectors = *model.sector1_length_m + *model.sector2_length_m + *model.sector3_length_m;
        if (std::fabs(sum_sectors - model.lap_length_m) > 5.0) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/sector1_length_m",
                "Sum of sector lengths deviates from lap_length_m by more than 5.0 m.",
                std::to_string(model.lap_length_m) + " +/- 5.0",
                std::to_string(sum_sectors));
        }
    }

    std::string dir_str;
    if (requireNonEmptyString(obj, "race_direction", "/race_direction", ctx, dir_str)) {
        auto dir = parseRaceDirection(dir_str);
        if (!dir.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/race_direction",
                "Invalid race_direction enum '" + dir_str + "'.",
                "CLOCKWISE|ANTICLOCKWISE|FIGURE_EIGHT",
                dir_str);
        } else {
            model.race_direction = *dir;
        }
    }

    if (requireDouble(obj, "pit_lane_length_m", "/pit_lane_length_m", ctx, model.pit_lane_length_m)) {
        checkRangeDouble(model.pit_lane_length_m, 200.0, 600.0, "/pit_lane_length_m", "pit_lane_length_m", ctx);
    }
    if (requireDouble(obj, "pit_speed_limit_kmh", "/pit_speed_limit_kmh", ctx, model.pit_speed_limit_kmh)) {
        checkRangeDouble(model.pit_speed_limit_kmh, 60.0, 80.0, "/pit_speed_limit_kmh", "pit_speed_limit_kmh", ctx);
    }

    bool ok_tot = requireInt(obj, "corners_total_count", "/corners_total_count", ctx, model.corners_total_count);
    bool ok_left = requireInt(obj, "corners_left_count", "/corners_left_count", ctx, model.corners_left_count);
    bool ok_right = requireInt(obj, "corners_right_count", "/corners_right_count", ctx, model.corners_right_count);
    if (ok_tot) {
        checkRangeInt(model.corners_total_count, 8, 28, "/corners_total_count", "corners_total_count", ctx);
    }
    if (ok_tot && ok_left && ok_right) {
        if (model.corners_left_count < 0 || model.corners_right_count < 0 ||
            model.corners_left_count + model.corners_right_count != model.corners_total_count) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/corners_total_count",
                "Corner count invariant failed: corners_left_count + corners_right_count must equal corners_total_count.",
                std::to_string(model.corners_total_count),
                std::to_string(model.corners_left_count + model.corners_right_count));
        }
    }

    readNullableDouble(obj, "frac_lap_straight", "/frac_lap_straight", true, true, ctx, model.frac_lap_straight);
    readNullableDouble(obj, "frac_lap_corner_slow", "/frac_lap_corner_slow", true, true, ctx, model.frac_lap_corner_slow);
    readNullableDouble(obj, "frac_lap_corner_med", "/frac_lap_corner_med", true, true, ctx, model.frac_lap_corner_med);
    readNullableDouble(obj, "frac_lap_corner_high", "/frac_lap_corner_high", true, true, ctx, model.frac_lap_corner_high);

    if (model.frac_lap_straight.has_value()) {
        checkRangeDouble(*model.frac_lap_straight, 0.20, 0.85, "/frac_lap_straight", "frac_lap_straight", ctx);
    }
    if (model.hasCompleteSpeedBandFractions()) {
        double sum_frac = *model.frac_lap_straight + *model.frac_lap_corner_slow +
                          *model.frac_lap_corner_med + *model.frac_lap_corner_high;
        if (std::fabs(sum_frac - 1.0) > 1e-3) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/frac_lap_straight",
                "Lap speed-band fractions must sum to 1.0 (+/- 0.001).",
                "1.0",
                std::to_string(sum_frac));
        }
    }

    if (requireDouble(obj, "lateral_energy_index_norm", "/lateral_energy_index_norm", ctx, model.lateral_energy_index_norm)) {
        checkRangeDouble(model.lateral_energy_index_norm, 0.40, 2.00, "/lateral_energy_index_norm", "lateral_energy_index_norm", ctx);
    }
    if (requireDouble(obj, "braking_energy_index_norm", "/braking_energy_index_norm", ctx, model.braking_energy_index_norm)) {
        checkRangeDouble(model.braking_energy_index_norm, 0.40, 2.00, "/braking_energy_index_norm", "braking_energy_index_norm", ctx);
    }
    if (requireDouble(obj, "traction_energy_index_norm", "/traction_energy_index_norm", ctx, model.traction_energy_index_norm)) {
        checkRangeDouble(model.traction_energy_index_norm, 0.40, 2.00, "/traction_energy_index_norm", "traction_energy_index_norm", ctx);
    }

    parseFieldProvenanceMap(obj, "field_provenance", "/field_provenance", ctx, model.field_provenance);

    emitDiagnostics(ctx.diagnostics);
    if (ctx.hasFailed()) {
        return Result<TrackLayoutVersion>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }
    return Result<TrackLayoutVersion>::success(std::move(model), std::move(ctx.diagnostics));
}

Result<TrackSurfaceProfile> DataAccessLayer::loadTrackSurface(
    const std::string& track_id,
    int surface_version,
    std::optional<int> expected_layout_version) const {
    auto resolved = resolver_.resolveTrackSurface(track_id, surface_version);
    if (!resolved.ok()) {
        emitDiagnostics(resolved.diagnostics);
        return Result<TrackSurfaceProfile>::failure(resolved.status, std::move(resolved.diagnostics));
    }

    const std::string dataset_id = track_id + "_surface_v" + std::to_string(surface_version);
    auto parsed = readAndParseJsonFile(*resolved.value, dataset_id);
    if (parsed.status != LoadStatus::Success) {
        emitDiagnostics(parsed.diagnostics);
        return Result<TrackSurfaceProfile>::failure(parsed.status, std::move(parsed.diagnostics));
    }

    const JsonValue& obj = *parsed.root;
    ValidationContext ctx{dataset_id, resolved.value->relative_path};
    TrackSurfaceProfile model;

    if (!validateSchemaHeader(obj, "TrackSurfaceProfile", ctx, model.schema_name, model.schema_version)) {
        emitDiagnostics(ctx.diagnostics);
        return Result<TrackSurfaceProfile>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }

    if (requireNonEmptyString(obj, "track_id", "/track_id", ctx, model.track_id)) {
        if (!isValidTrackOrEventSlug(model.track_id)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/track_id",
                "track_id must match pattern [a-z0-9_]+.",
                "[a-z0-9_]+",
                model.track_id);
        } else if (model.track_id != track_id) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/track_id",
                "Loaded TrackSurfaceProfile track_id does not match requested track_id.",
                track_id,
                model.track_id);
        }
    }

    if (requireInt(obj, "layout_version", "/layout_version", ctx, model.layout_version)) {
        if (model.layout_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/layout_version",
                "layout_version must be >= 1.",
                ">= 1",
                std::to_string(model.layout_version));
        } else if (expected_layout_version.has_value() && model.layout_version != *expected_layout_version) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/layout_version",
                "TrackSurfaceProfile layout_version does not match expected layout_version.",
                std::to_string(*expected_layout_version),
                std::to_string(model.layout_version));
        }
    }

    if (requireInt(obj, "surface_version", "/surface_version", ctx, model.surface_version)) {
        if (model.surface_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/surface_version",
                "surface_version must be >= 1.",
                ">= 1",
                std::to_string(model.surface_version));
        } else if (model.surface_version != surface_version) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/surface_version",
                "Loaded TrackSurfaceProfile surface_version does not match requested surface_version.",
                std::to_string(surface_version),
                std::to_string(model.surface_version));
        }
    }

    if (requireNonEmptyString(obj, "valid_from_date", "/valid_from_date", ctx, model.valid_from_date)) {
        if (!isValidIsoDate(model.valid_from_date)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/valid_from_date",
                "valid_from_date must be an ISO-8601 YYYY-MM-DD date.",
                "YYYY-MM-DD",
                model.valid_from_date);
        }
    }
    readOptionalString(obj, "valid_to_date", "/valid_to_date", true, ctx, model.valid_to_date);

    if (requireDouble(obj, "surface_abrasiveness_score", "/surface_abrasiveness_score", ctx, model.surface_abrasiveness_score)) {
        checkRangeDouble(model.surface_abrasiveness_score, 1.0, 5.0, "/surface_abrasiveness_score", "surface_abrasiveness_score", ctx);
    }
    if (requireDouble(obj, "surface_grip_baseline_score", "/surface_grip_baseline_score", ctx, model.surface_grip_baseline_score)) {
        checkRangeDouble(model.surface_grip_baseline_score, 1.0, 5.0, "/surface_grip_baseline_score", "surface_grip_baseline_score", ctx);
    }

    readNullableDouble(obj, "bumpiness_index_score", "/bumpiness_index_score", true, true, ctx, model.bumpiness_index_score);
    if (model.bumpiness_index_score.has_value()) {
        checkRangeDouble(*model.bumpiness_index_score, 1.0, 5.0, "/bumpiness_index_score", "bumpiness_index_score", ctx);
    }

    if (requireDouble(obj, "track_evolution_amplitude_s", "/track_evolution_amplitude_s", ctx, model.track_evolution_amplitude_s)) {
        checkRangeDouble(model.track_evolution_amplitude_s, 0.0, 2.5, "/track_evolution_amplitude_s", "track_evolution_amplitude_s", ctx);
    }
    if (requireDouble(obj, "track_evolution_decay_laps", "/track_evolution_decay_laps", ctx, model.track_evolution_decay_laps)) {
        checkRangeDouble(model.track_evolution_decay_laps, 5.0, 60.0, "/track_evolution_decay_laps", "track_evolution_decay_laps", ctx);
    }

    readOptionalString(obj, "surface_description_notes", "/surface_description_notes", false, ctx, model.surface_description_notes);
    parseFieldProvenanceMap(obj, "field_provenance", "/field_provenance", ctx, model.field_provenance);

    emitDiagnostics(ctx.diagnostics);
    if (ctx.hasFailed()) {
        return Result<TrackSurfaceProfile>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }
    return Result<TrackSurfaceProfile>::success(std::move(model), std::move(ctx.diagnostics));
}

Result<EventConfiguration> DataAccessLayer::loadEventConfiguration(
    const std::string& event_id) const {
    auto resolved = resolver_.resolveEventConfiguration(event_id);
    if (!resolved.ok()) {
        emitDiagnostics(resolved.diagnostics);
        return Result<EventConfiguration>::failure(resolved.status, std::move(resolved.diagnostics));
    }

    auto parsed = readAndParseJsonFile(*resolved.value, event_id);
    if (parsed.status != LoadStatus::Success) {
        emitDiagnostics(parsed.diagnostics);
        return Result<EventConfiguration>::failure(parsed.status, std::move(parsed.diagnostics));
    }

    const JsonValue& obj = *parsed.root;
    ValidationContext ctx{event_id, resolved.value->relative_path};
    EventConfiguration model;

    if (!validateSchemaHeader(obj, "EventConfiguration", ctx, model.schema_name, model.schema_version)) {
        emitDiagnostics(ctx.diagnostics);
        return Result<EventConfiguration>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }

    if (requireNonEmptyString(obj, "event_id", "/event_id", ctx, model.event_id)) {
        if (!isValidTrackOrEventSlug(model.event_id)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/event_id",
                "event_id must match pattern [a-z0-9_]+.",
                "[a-z0-9_]+",
                model.event_id);
        } else if (model.event_id != event_id) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/event_id",
                "Loaded EventConfiguration event_id does not match requested event_id.",
                event_id,
                model.event_id);
        }
    }

    requireNonEmptyString(obj, "official_event_name", "/official_event_name", ctx, model.official_event_name);
    if (requireInt(obj, "round_number", "/round_number", ctx, model.round_number)) {
        checkRangeInt(model.round_number, 1, 30, "/round_number", "round_number", ctx);
    }

    const JsonValue* dates_obj = obj.findKey("event_dates");
    if (dates_obj == nullptr || !dates_obj->isObject()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/event_dates",
            "Missing or non-object required field 'event_dates'.",
            "object",
            dates_obj ? dates_obj->typeName() : "missing");
    } else {
        readOptionalString(*dates_obj, "fp1_and_sprint_qualifying_date", "/event_dates/fp1_and_sprint_qualifying_date", false, ctx, model.event_dates.fp1_and_sprint_qualifying_date);
        readOptionalString(*dates_obj, "sprint_and_qualifying_date", "/event_dates/sprint_and_qualifying_date", false, ctx, model.event_dates.sprint_and_qualifying_date);
        readOptionalString(*dates_obj, "fp1_and_fp2_date", "/event_dates/fp1_and_fp2_date", false, ctx, model.event_dates.fp1_and_fp2_date);
        readOptionalString(*dates_obj, "fp3_and_qualifying_date", "/event_dates/fp3_and_qualifying_date", false, ctx, model.event_dates.fp3_and_qualifying_date);
        if (requireNonEmptyString(*dates_obj, "race_date", "/event_dates/race_date", ctx, model.event_dates.race_date)) {
            if (!isValidIsoDate(model.event_dates.race_date)) {
                ctx.recordFailure(
                    LoadStatus::SemanticValidationFailed,
                    DiagnosticCode::InvalidRangeOrEnum,
                    "/event_dates/race_date",
                    "race_date must be an ISO-8601 YYYY-MM-DD date.",
                    "YYYY-MM-DD",
                    model.event_dates.race_date);
            }
        }
    }

    std::string fmt_str;
    if (requireNonEmptyString(obj, "competition_format", "/competition_format", ctx, fmt_str)) {
        auto fmt = parseCompetitionFormat(fmt_str);
        if (!fmt.has_value()) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/competition_format",
                "Invalid competition_format enum '" + fmt_str + "'.",
                "STANDARD|ALTERNATIVE_FORMAT_SPRINT",
                fmt_str);
        } else {
            model.competition_format = *fmt;
        }
    }

    if (requireInt(obj, "season_year", "/season_year", ctx, model.season_year)) {
        checkRangeInt(model.season_year, 2018, 2035, "/season_year", "season_year", ctx);
    }

    if (requireNonEmptyString(obj, "track_id", "/track_id", ctx, model.track_id)) {
        if (!isValidTrackOrEventSlug(model.track_id)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/track_id",
                "track_id must match pattern [a-z0-9_]+.",
                "[a-z0-9_]+",
                model.track_id);
        }
    }
    if (requireInt(obj, "layout_version", "/layout_version", ctx, model.layout_version)) {
        if (model.layout_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/layout_version",
                "layout_version must be >= 1.",
                ">= 1",
                std::to_string(model.layout_version));
        }
    }
    if (requireInt(obj, "surface_version", "/surface_version", ctx, model.surface_version)) {
        if (model.surface_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/surface_version",
                "surface_version must be >= 1.",
                ">= 1",
                std::to_string(model.surface_version));
        }
    }

    bool ok_laps = requireInt(obj, "race_distance_laps", "/race_distance_laps", ctx, model.race_distance_laps);
    if (ok_laps) {
        checkRangeInt(model.race_distance_laps, 1, 100, "/race_distance_laps", "race_distance_laps", ctx);
    }
    if (requireDouble(obj, "scheduled_race_distance_km", "/scheduled_race_distance_km", ctx, model.scheduled_race_distance_km)) {
        checkRangeDouble(model.scheduled_race_distance_km, 3.0, 400.0, "/scheduled_race_distance_km", "scheduled_race_distance_km", ctx);
    }

    parseNominatedCompoundsTriplet(
        obj, "nominated_compounds_set", "/nominated_compounds_set", ctx, model.nominated_compounds_set);

    requireBool(obj, "is_hypothetical_override", "/is_hypothetical_override", ctx, model.is_hypothetical_override);

    if (requireInt(obj, "min_distinct_dry_compounds", "/min_distinct_dry_compounds", ctx, model.min_distinct_dry_compounds)) {
        checkRangeInt(model.min_distinct_dry_compounds, 1, 3, "/min_distinct_dry_compounds", "min_distinct_dry_compounds", ctx);
    }

    bool ok_pmin = requireInt(obj, "min_pit_stops", "/min_pit_stops", ctx, model.min_pit_stops);
    bool ok_pmax = requireInt(obj, "max_pit_stops", "/max_pit_stops", ctx, model.max_pit_stops);
    if (ok_pmin && ok_pmax) {
        if (model.min_pit_stops < 0 || model.max_pit_stops > 6 || model.min_pit_stops > model.max_pit_stops) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/min_pit_stops",
                "Pit-stop bounds must satisfy 0 <= min_pit_stops <= max_pit_stops <= 6.",
                "0 <= min_pit_stops <= max_pit_stops <= 6",
                std::to_string(model.min_pit_stops) + " .. " + std::to_string(model.max_pit_stops));
        }
    }

    if (requireInt(obj, "min_stint_length_laps", "/min_stint_length_laps", ctx, model.min_stint_length_laps) && ok_laps) {
        checkRangeInt(model.min_stint_length_laps, 1, model.race_distance_laps, "/min_stint_length_laps", "min_stint_length_laps", ctx);
    }

    readOptionalInt(obj, "max_stint_length_laps", "/max_stint_length_laps", true, ctx, model.max_stint_length_laps);
    if (model.max_stint_length_laps.has_value() && ok_laps) {
        checkRangeInt(*model.max_stint_length_laps, model.min_stint_length_laps, model.race_distance_laps, "/max_stint_length_laps", "max_stint_length_laps", ctx);
    }

    if (requireInt(obj, "available_sets_hard", "/available_sets_hard", ctx, model.available_sets_hard)) {
        checkRangeInt(model.available_sets_hard, 1, 8, "/available_sets_hard", "available_sets_hard", ctx);
    }
    if (requireInt(obj, "available_sets_med", "/available_sets_med", ctx, model.available_sets_med)) {
        checkRangeInt(model.available_sets_med, 1, 8, "/available_sets_med", "available_sets_med", ctx);
    }
    if (requireInt(obj, "available_sets_soft", "/available_sets_soft", ctx, model.available_sets_soft)) {
        checkRangeInt(model.available_sets_soft, 1, 8, "/available_sets_soft", "available_sets_soft", ctx);
    }

    readOptionalString(
        obj,
        "typical_pre_race_sunday_remaining_sets_note",
        "/typical_pre_race_sunday_remaining_sets_note",
        false,
        ctx,
        model.typical_pre_race_sunday_remaining_sets_note);

    const JsonValue* regs_obj = obj.findKey("governing_regulations");
    if (regs_obj == nullptr || !regs_obj->isObject()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/governing_regulations",
            "Missing or non-object required field 'governing_regulations'.",
            "object",
            regs_obj ? regs_obj->typeName() : "missing");
    } else {
        requireNonEmptyString(*regs_obj, "sporting_regulations_doc", "/governing_regulations/sporting_regulations_doc", ctx, model.governing_regulations.sporting_regulations_doc);
        requireNonEmptyString(*regs_obj, "technical_regulations_doc", "/governing_regulations/technical_regulations_doc", ctx, model.governing_regulations.technical_regulations_doc);
    }

    parseFieldProvenanceMap(obj, "field_provenance", "/field_provenance", ctx, model.field_provenance);

    emitDiagnostics(ctx.diagnostics);
    if (ctx.hasFailed()) {
        return Result<EventConfiguration>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }
    return Result<EventConfiguration>::success(std::move(model), std::move(ctx.diagnostics));
}

Result<EventRoleMapping> DataAccessLayer::loadEventRoleMapping(
    const std::string& event_id) const {
    auto resolved = resolver_.resolveEventRoleMapping(event_id);
    if (!resolved.ok()) {
        emitDiagnostics(resolved.diagnostics);
        return Result<EventRoleMapping>::failure(resolved.status, std::move(resolved.diagnostics));
    }

    const std::string dataset_id = event_id + "_role_mapping";
    auto parsed = readAndParseJsonFile(*resolved.value, dataset_id);
    if (parsed.status != LoadStatus::Success) {
        emitDiagnostics(parsed.diagnostics);
        return Result<EventRoleMapping>::failure(parsed.status, std::move(parsed.diagnostics));
    }

    const JsonValue& obj = *parsed.root;
    ValidationContext ctx{dataset_id, resolved.value->relative_path};
    EventRoleMapping model;

    if (!validateSchemaHeader(obj, "EventRoleMapping", ctx, model.schema_name, model.schema_version)) {
        emitDiagnostics(ctx.diagnostics);
        return Result<EventRoleMapping>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }

    if (requireNonEmptyString(obj, "event_id", "/event_id", ctx, model.event_id)) {
        if (!isValidTrackOrEventSlug(model.event_id)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/event_id",
                "event_id must match pattern [a-z0-9_]+.",
                "[a-z0-9_]+",
                model.event_id);
        } else if (model.event_id != event_id) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/event_id",
                "Loaded EventRoleMapping event_id does not match requested event_id.",
                event_id,
                model.event_id);
        }
    }

    if (requireInt(obj, "season_year", "/season_year", ctx, model.season_year)) {
        checkRangeInt(model.season_year, 2018, 2035, "/season_year", "season_year", ctx);
    }
    if (requireInt(obj, "spec_version", "/spec_version", ctx, model.spec_version)) {
        if (model.spec_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/spec_version",
                "spec_version must be >= 1.",
                ">= 1",
                std::to_string(model.spec_version));
        }
    }

    requireBool(obj, "is_hypothetical_override", "/is_hypothetical_override", ctx, model.is_hypothetical_override);
    bool nominated_ok = parseNominatedCompoundsTriplet(
        obj, "nominated_compounds_set", "/nominated_compounds_set", ctx, model.nominated_compounds_set);

    const JsonValue* mappings_val = obj.findKey("mappings");
    if (mappings_val == nullptr || !mappings_val->isArray()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/mappings",
            "Required field 'mappings' must be a JSON array of 3 role mapping objects.",
            "array of 3 objects",
            mappings_val ? mappings_val->typeName() : "missing");
    } else {
        const auto& arr = mappings_val->asArray();
        if (arr.size() != 3) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/mappings",
                "EventRoleMapping 'mappings' must contain exactly 3 entries for {HARD, MEDIUM, SOFT}.",
                "3 entries",
                std::to_string(arr.size()));
        }

        std::set<TyreRole> seen_roles;
        std::set<PhysicalCompoundId> mapped_compounds;
        std::set<PhysicalCompoundId> nominated_set(
            model.nominated_compounds_set.begin(), model.nominated_compounds_set.end());

        for (std::size_t i = 0; i < arr.size(); ++i) {
            const std::string entry_ptr = "/mappings/" + std::to_string(i);
            if (!arr[i].isObject()) {
                ctx.recordFailure(
                    LoadStatus::SchemaValidationFailed,
                    DiagnosticCode::StructuralSchemaFailure,
                    entry_ptr,
                    "Mapping entry must be a JSON object.",
                    "object",
                    arr[i].typeName());
                continue;
            }
            RoleMappingEntry entry;
            if (requireNonEmptyString(arr[i], "event_id", entry_ptr + "/event_id", ctx, entry.event_id)) {
                if (!model.event_id.empty() && entry.event_id != model.event_id) {
                    ctx.recordFailure(
                        LoadStatus::CrossReferenceFailed,
                        DiagnosticCode::CrossReferenceFailure,
                        entry_ptr + "/event_id",
                        "Mapping entry event_id does not match parent EventRoleMapping event_id.",
                        model.event_id,
                        entry.event_id);
                }
            }

            std::string role_str;
            if (requireNonEmptyString(arr[i], "role_id", entry_ptr + "/role_id", ctx, role_str)) {
                auto role = parseTyreRole(role_str);
                if (!role.has_value()) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        entry_ptr + "/role_id",
                        "Invalid tyre role '" + role_str + "'; expected HARD, MEDIUM, or SOFT.",
                        "HARD|MEDIUM|SOFT",
                        role_str);
                } else {
                    entry.role_id = *role;
                    if (!seen_roles.insert(*role).second) {
                        ctx.recordFailure(
                            LoadStatus::SemanticValidationFailed,
                            DiagnosticCode::InvalidRangeOrEnum,
                            entry_ptr + "/role_id",
                            "Duplicate role_id '" + role_str + "' in EventRoleMapping.",
                            "Distinct roles {HARD, MEDIUM, SOFT}",
                            role_str);
                    }
                }
            }

            std::string comp_str;
            if (requireNonEmptyString(arr[i], "compound_id", entry_ptr + "/compound_id", ctx, comp_str)) {
                auto cid = parsePhysicalCompoundId(comp_str);
                if (!cid.has_value()) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        entry_ptr + "/compound_id",
                        "Invalid physical compound_id '" + comp_str + "'; expected C1..C5.",
                        "C1|C2|C3|C4|C5",
                        comp_str);
                } else {
                    entry.compound_id = *cid;
                    if (!mapped_compounds.insert(*cid).second) {
                        ctx.recordFailure(
                            LoadStatus::SemanticValidationFailed,
                            DiagnosticCode::InvalidRangeOrEnum,
                            entry_ptr + "/compound_id",
                            "Non-injective role mapping: multiple roles assigned to compound '" + comp_str + "'.",
                            "Injective mapping onto 3 distinct compounds",
                            comp_str);
                    }
                    if (nominated_ok && nominated_set.find(*cid) == nominated_set.end()) {
                        ctx.recordFailure(
                            LoadStatus::CrossReferenceFailed,
                            DiagnosticCode::CrossReferenceFailure,
                            entry_ptr + "/compound_id",
                            "Mapped compound '" + comp_str + "' is not in nominated_compounds_set (ERR_ROLE_MAPPING_MISMATCH_NOMINATED_SET).",
                            "Element of nominated_compounds_set",
                            comp_str);
                    }
                }
            }

            requireNonEmptyString(arr[i], "sidewall_colour", entry_ptr + "/sidewall_colour", ctx, entry.sidewall_colour);
            if (requireInt(arr[i], "hardness_rank", entry_ptr + "/hardness_rank", ctx, entry.hardness_rank)) {
                checkRangeInt(entry.hardness_rank, 1, 5, entry_ptr + "/hardness_rank", "hardness_rank", ctx);
                if (entry.hardness_rank != canonicalHardnessRank(entry.compound_id)) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        entry_ptr + "/hardness_rank",
                        "hardness_rank does not match canonical hardness rank of compound.",
                        std::to_string(canonicalHardnessRank(entry.compound_id)),
                        std::to_string(entry.hardness_rank));
                }
            }

            model.mappings.push_back(std::move(entry));
        }

        if (seen_roles.size() == 3 && mapped_compounds.size() == 3 && nominated_ok) {
            if (mapped_compounds != nominated_set) {
                ctx.recordFailure(
                    LoadStatus::CrossReferenceFailed,
                    DiagnosticCode::CrossReferenceFailure,
                    "/mappings",
                    "Image of role mapping {S, M, H} does not equal nominated_compounds_set.",
                    "Exact bijection onto nominated_compounds_set",
                    "mismatch");
            }
            if (!model.is_hypothetical_override) {
                const auto* h_entry = model.entryForRole(TyreRole::Hard);
                const auto* m_entry = model.entryForRole(TyreRole::Medium);
                const auto* s_entry = model.entryForRole(TyreRole::Soft);
                if (h_entry && m_entry && s_entry) {
                    if (!(h_entry->hardness_rank < m_entry->hardness_rank &&
                          m_entry->hardness_rank < s_entry->hardness_rank)) {
                        ctx.recordFailure(
                            LoadStatus::SemanticValidationFailed,
                            DiagnosticCode::InvalidRangeOrEnum,
                            "/mappings",
                            "Hardness rank ordering violation: official event requires rank(HARD) < rank(MEDIUM) < rank(SOFT).",
                            "rank(HARD) < rank(MEDIUM) < rank(SOFT)",
                            std::to_string(h_entry->hardness_rank) + ", " +
                                std::to_string(m_entry->hardness_rank) + ", " +
                                std::to_string(s_entry->hardness_rank));
                    }
                }
            }
        }
    }

    const JsonValue* prov = obj.findKey("provenance");
    if (prov == nullptr) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/provenance",
            "Missing required 'provenance' object in EventRoleMapping.",
            "object",
            "missing");
    } else {
        parseProvenanceObject(*prov, "/provenance", ctx, model.provenance);
    }

    emitDiagnostics(ctx.diagnostics);
    if (ctx.hasFailed()) {
        return Result<EventRoleMapping>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }
    return Result<EventRoleMapping>::success(std::move(model), std::move(ctx.diagnostics));
}

Result<PhysicalCompoundCatalogue> DataAccessLayer::loadCompoundCatalogue(
    int season_year,
    int spec_version) const {
    auto resolved = resolver_.resolveCompoundCatalogue(season_year, spec_version);
    if (!resolved.ok()) {
        emitDiagnostics(resolved.diagnostics);
        return Result<PhysicalCompoundCatalogue>::failure(resolved.status, std::move(resolved.diagnostics));
    }

    const std::string dataset_id =
        std::to_string(season_year) + "_compound_catalogue_v" + std::to_string(spec_version);
    auto parsed = readAndParseJsonFile(*resolved.value, dataset_id);
    if (parsed.status != LoadStatus::Success) {
        emitDiagnostics(parsed.diagnostics);
        return Result<PhysicalCompoundCatalogue>::failure(parsed.status, std::move(parsed.diagnostics));
    }

    const JsonValue& obj = *parsed.root;
    ValidationContext ctx{dataset_id, resolved.value->relative_path};
    PhysicalCompoundCatalogue model;

    if (!validateSchemaHeader(obj, "PhysicalCompoundCatalogue", ctx, model.schema_name, model.schema_version)) {
        emitDiagnostics(ctx.diagnostics);
        return Result<PhysicalCompoundCatalogue>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }

    if (requireInt(obj, "season_year", "/season_year", ctx, model.season_year)) {
        checkRangeInt(model.season_year, 2018, 2035, "/season_year", "season_year", ctx);
        if (model.season_year != season_year) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/season_year",
                "Loaded PhysicalCompoundCatalogue season_year does not match requested season_year.",
                std::to_string(season_year),
                std::to_string(model.season_year));
        }
    }

    if (requireInt(obj, "spec_version", "/spec_version", ctx, model.spec_version)) {
        if (model.spec_version < 1) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/spec_version",
                "spec_version must be >= 1.",
                ">= 1",
                std::to_string(model.spec_version));
        } else if (model.spec_version != spec_version) {
            ctx.recordFailure(
                LoadStatus::CrossReferenceFailed,
                DiagnosticCode::CrossReferenceFailure,
                "/spec_version",
                "Loaded PhysicalCompoundCatalogue spec_version does not match requested spec_version.",
                std::to_string(spec_version),
                std::to_string(model.spec_version));
        }
    }

    if (requireNonEmptyString(obj, "homologation_date", "/homologation_date", ctx, model.homologation_date)) {
        if (!isValidIsoDate(model.homologation_date)) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/homologation_date",
                "homologation_date must be an ISO-8601 YYYY-MM-DD date.",
                "YYYY-MM-DD",
                model.homologation_date);
        }
    }
    requireNonEmptyString(obj, "calibration_status", "/calibration_status", ctx, model.calibration_status);

    const JsonValue* comps_val = obj.findKey("compounds");
    if (comps_val == nullptr || !comps_val->isArray()) {
        ctx.recordFailure(
            LoadStatus::SchemaValidationFailed,
            DiagnosticCode::StructuralSchemaFailure,
            "/compounds",
            "Required field 'compounds' must be a JSON array of 5 compound records (C1..C5).",
            "array of 5 objects",
            comps_val ? comps_val->typeName() : "missing");
    } else {
        const auto& arr = comps_val->asArray();
        if (arr.size() != 5) {
            ctx.recordFailure(
                LoadStatus::SemanticValidationFailed,
                DiagnosticCode::InvalidRangeOrEnum,
                "/compounds",
                "PhysicalCompoundCatalogue 'compounds' must contain all 5 dry slick compounds C1..C5.",
                "5 compounds (C1..C5)",
                std::to_string(arr.size()));
        }

        std::set<PhysicalCompoundId> seen_compounds;
        for (std::size_t i = 0; i < arr.size(); ++i) {
            const std::string cptr = "/compounds/" + std::to_string(i);
            if (!arr[i].isObject()) {
                ctx.recordFailure(
                    LoadStatus::SchemaValidationFailed,
                    DiagnosticCode::StructuralSchemaFailure,
                    cptr,
                    "Compound entry must be a JSON object.",
                    "object",
                    arr[i].typeName());
                continue;
            }

            PhysicalCompoundSpec spec;
            if (requireInt(arr[i], "season_year", cptr + "/season_year", ctx, spec.season_year)) {
                if (spec.season_year != model.season_year) {
                    ctx.recordFailure(
                        LoadStatus::CrossReferenceFailed,
                        DiagnosticCode::CrossReferenceFailure,
                        cptr + "/season_year",
                        "Compound entry season_year does not match catalogue season_year.",
                        std::to_string(model.season_year),
                        std::to_string(spec.season_year));
                }
            }
            if (requireInt(arr[i], "spec_version", cptr + "/spec_version", ctx, spec.spec_version)) {
                if (spec.spec_version != model.spec_version) {
                    ctx.recordFailure(
                        LoadStatus::CrossReferenceFailed,
                        DiagnosticCode::CrossReferenceFailure,
                        cptr + "/spec_version",
                        "Compound entry spec_version does not match catalogue spec_version.",
                        std::to_string(model.spec_version),
                        std::to_string(spec.spec_version));
                }
            }

            std::string cid_str;
            if (requireNonEmptyString(arr[i], "compound_id", cptr + "/compound_id", ctx, cid_str)) {
                auto cid = parsePhysicalCompoundId(cid_str);
                if (!cid.has_value()) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        cptr + "/compound_id",
                        "Invalid compound_id '" + cid_str + "'; expected C1..C5.",
                        "C1|C2|C3|C4|C5",
                        cid_str);
                } else {
                    spec.compound_id = *cid;
                    if (!seen_compounds.insert(*cid).second) {
                        ctx.recordFailure(
                            LoadStatus::SemanticValidationFailed,
                            DiagnosticCode::InvalidRangeOrEnum,
                            cptr + "/compound_id",
                            "Duplicate compound_id '" + cid_str + "' in PhysicalCompoundCatalogue.",
                            "Distinct C1..C5",
                            cid_str);
                    }
                }
            }

            if (requireInt(arr[i], "hardness_rank", cptr + "/hardness_rank", ctx, spec.hardness_rank)) {
                checkRangeInt(spec.hardness_rank, 1, 5, cptr + "/hardness_rank", "hardness_rank", ctx);
                if (spec.hardness_rank != canonicalHardnessRank(spec.compound_id)) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        cptr + "/hardness_rank",
                        "hardness_rank does not match canonical hardness rank of compound_id.",
                        std::to_string(canonicalHardnessRank(spec.compound_id)),
                        std::to_string(spec.hardness_rank));
                }
            }

            requireNonEmptyString(arr[i], "description", cptr + "/description", ctx, spec.description);

            readNullableDouble(arr[i], "front_tyre_width_reduction_vs_2025_mm", cptr + "/front_tyre_width_reduction_vs_2025_mm", false, false, ctx, spec.front_tyre_width_reduction_vs_2025_mm);
            readNullableDouble(arr[i], "rear_tyre_width_reduction_vs_2025_mm", cptr + "/rear_tyre_width_reduction_vs_2025_mm", false, false, ctx, spec.rear_tyre_width_reduction_vs_2025_mm);
            readNullableDouble(arr[i], "front_outer_diameter_reduction_vs_2025_mm", cptr + "/front_outer_diameter_reduction_vs_2025_mm", false, false, ctx, spec.front_outer_diameter_reduction_vs_2025_mm);
            readNullableDouble(arr[i], "rear_outer_diameter_reduction_vs_2025_mm", cptr + "/rear_outer_diameter_reduction_vs_2025_mm", false, false, ctx, spec.rear_outer_diameter_reduction_vs_2025_mm);
            readOptionalInt(arr[i], "wheel_rim_diameter_in", cptr + "/wheel_rim_diameter_in", false, ctx, spec.wheel_rim_diameter_in);

            readNullableDouble(arr[i], "pace_offset_ref_s", cptr + "/pace_offset_ref_s", true, true, ctx, spec.pace_offset_ref_s);
            if (spec.pace_offset_ref_s.has_value()) {
                checkRangeDouble(*spec.pace_offset_ref_s, -2.0, 2.0, cptr + "/pace_offset_ref_s", "pace_offset_ref_s", ctx);
                if (spec.compound_id == PhysicalCompoundId::C3 && std::fabs(*spec.pace_offset_ref_s) > 1e-9) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        cptr + "/pace_offset_ref_s",
                        "Reference compound C3 must have pace_offset_ref_s == 0.0 s by definition (Eq. 6.4).",
                        "0.0",
                        std::to_string(*spec.pace_offset_ref_s));
                }
            }

            readNullableDouble(arr[i], "base_wear_rate_per_lap", cptr + "/base_wear_rate_per_lap", true, true, ctx, spec.base_wear_rate_per_lap);
            if (spec.base_wear_rate_per_lap.has_value()) {
                checkRangeDouble(*spec.base_wear_rate_per_lap, 0.005, 0.080, cptr + "/base_wear_rate_per_lap", "base_wear_rate_per_lap", ctx);
            }

            readNullableDouble(arr[i], "linear_deg_coeff_s", cptr + "/linear_deg_coeff_s", true, true, ctx, spec.linear_deg_coeff_s);
            if (spec.linear_deg_coeff_s.has_value()) {
                checkRangeDouble(*spec.linear_deg_coeff_s, 0.5, 8.0, cptr + "/linear_deg_coeff_s", "linear_deg_coeff_s", ctx);
            }

            readNullableDouble(arr[i], "cliff_wear_threshold", cptr + "/cliff_wear_threshold", true, true, ctx, spec.cliff_wear_threshold);
            if (spec.cliff_wear_threshold.has_value()) {
                checkRangeDouble(*spec.cliff_wear_threshold, 0.50, 1.00, cptr + "/cliff_wear_threshold", "cliff_wear_threshold", ctx);
            }

            readNullableDouble(arr[i], "cliff_amplitude_s", cptr + "/cliff_amplitude_s", true, true, ctx, spec.cliff_amplitude_s);
            if (spec.cliff_amplitude_s.has_value()) {
                checkRangeDouble(*spec.cliff_amplitude_s, 0.0, 10.0, cptr + "/cliff_amplitude_s", "cliff_amplitude_s", ctx);
            }

            readNullableDouble(arr[i], "cliff_exponent", cptr + "/cliff_exponent", true, true, ctx, spec.cliff_exponent);
            if (spec.cliff_exponent.has_value()) {
                checkRangeDouble(*spec.cliff_exponent, 1.5, 4.0, cptr + "/cliff_exponent", "cliff_exponent", ctx);
            }

            readNullableDouble(arr[i], "thermal_deg_sens_per_c", cptr + "/thermal_deg_sens_per_c", true, true, ctx, spec.thermal_deg_sens_per_c);
            if (spec.thermal_deg_sens_per_c.has_value()) {
                checkRangeDouble(*spec.thermal_deg_sens_per_c, 0.0, 0.05, cptr + "/thermal_deg_sens_per_c", "thermal_deg_sens_per_c", ctx);
            }

            readNullableDouble(arr[i], "opt_temp_window_min_c", cptr + "/opt_temp_window_min_c", true, true, ctx, spec.opt_temp_window_min_c);
            readNullableDouble(arr[i], "opt_temp_window_max_c", cptr + "/opt_temp_window_max_c", true, true, ctx, spec.opt_temp_window_max_c);
            if (spec.opt_temp_window_min_c.has_value()) {
                checkRangeDouble(*spec.opt_temp_window_min_c, 70.0, 145.0, cptr + "/opt_temp_window_min_c", "opt_temp_window_min_c", ctx);
            }
            if (spec.opt_temp_window_max_c.has_value()) {
                checkRangeDouble(*spec.opt_temp_window_max_c, 70.0, 145.0, cptr + "/opt_temp_window_max_c", "opt_temp_window_max_c", ctx);
            }
            if (spec.opt_temp_window_min_c.has_value() && spec.opt_temp_window_max_c.has_value() &&
                *spec.opt_temp_window_min_c >= *spec.opt_temp_window_max_c) {
                ctx.recordFailure(
                    LoadStatus::SemanticValidationFailed,
                    DiagnosticCode::InvalidRangeOrEnum,
                    cptr + "/opt_temp_window_min_c",
                    "opt_temp_window_min_c must be strictly less than opt_temp_window_max_c.",
                    "min < max",
                    std::to_string(*spec.opt_temp_window_min_c) + " >= " + std::to_string(*spec.opt_temp_window_max_c));
            }

            parseFieldProvenanceMap(arr[i], "field_provenance", cptr + "/field_provenance", ctx, spec.field_provenance);
            model.compounds.push_back(std::move(spec));
        }

        for (std::size_t a = 0; a < model.compounds.size(); ++a) {
            for (std::size_t b = a + 1; b < model.compounds.size(); ++b) {
                const auto& ca = model.compounds[a];
                const auto& cb = model.compounds[b];
                const auto& harder = (ca.hardness_rank < cb.hardness_rank) ? ca : cb;
                const auto& softer = (ca.hardness_rank < cb.hardness_rank) ? cb : ca;
                if (harder.pace_offset_ref_s.has_value() && softer.pace_offset_ref_s.has_value() &&
                    !(*harder.pace_offset_ref_s > *softer.pace_offset_ref_s)) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        "/compounds",
                        "Compound pace monotonicity violated: harder compound must have higher pace_offset_ref_s than softer compound.",
                        std::string(toString(harder.compound_id)) + " > " + toString(softer.compound_id),
                        std::to_string(*harder.pace_offset_ref_s) + " <= " + std::to_string(*softer.pace_offset_ref_s));
                }
                if (harder.base_wear_rate_per_lap.has_value() && softer.base_wear_rate_per_lap.has_value() &&
                    !(*harder.base_wear_rate_per_lap < *softer.base_wear_rate_per_lap)) {
                    ctx.recordFailure(
                        LoadStatus::SemanticValidationFailed,
                        DiagnosticCode::InvalidRangeOrEnum,
                        "/compounds",
                        "Compound wear rate monotonicity violated: harder compound must have smaller base_wear_rate_per_lap than softer compound.",
                        std::string(toString(harder.compound_id)) + " < " + toString(softer.compound_id),
                        std::to_string(*harder.base_wear_rate_per_lap) + " >= " + std::to_string(*softer.base_wear_rate_per_lap));
                }
            }
        }
    }

    emitDiagnostics(ctx.diagnostics);
    if (ctx.hasFailed()) {
        return Result<PhysicalCompoundCatalogue>::failure(ctx.failure_status, std::move(ctx.diagnostics));
    }
    return Result<PhysicalCompoundCatalogue>::success(std::move(model), std::move(ctx.diagnostics));
}

Result<PhysicalCompoundSpec> DataAccessLayer::loadCompoundSpec(
    int season_year,
    int spec_version,
    PhysicalCompoundId compound_id) const {
    auto cat_res = loadCompoundCatalogue(season_year, spec_version);
    if (!cat_res.ok()) {
        return Result<PhysicalCompoundSpec>::failure(cat_res.status, std::move(cat_res.diagnostics));
    }
    const PhysicalCompoundSpec* found = cat_res.value->findCompound(compound_id);
    if (found == nullptr) {
        Diagnostic d{
            Severity::Error,
            DiagnosticCode::CrossReferenceFailure,
            std::string("Requested compound ") + toString(compound_id) + " not found in catalogue.",
            std::to_string(season_year) + "_compound_catalogue_v" + std::to_string(spec_version),
            "",
            "/compounds",
            toString(compound_id),
            "not found"
        };
        emitDiagnostics({d});
        return Result<PhysicalCompoundSpec>::failure(LoadStatus::CrossReferenceFailed, {d});
    }
    return Result<PhysicalCompoundSpec>::success(*found, std::move(cat_res.diagnostics));
}

Result<EventBundle> DataAccessLayer::loadEventBundle(const std::string& event_id) const {
    std::vector<Diagnostic> all_diagnostics;

    auto appendDiags = [&all_diagnostics](const std::vector<Diagnostic>& diags) {
        all_diagnostics.insert(all_diagnostics.end(), diags.begin(), diags.end());
    };

    auto cfg_res = loadEventConfiguration(event_id);
    appendDiags(cfg_res.diagnostics);
    if (!cfg_res.ok()) {
        return Result<EventBundle>::failure(cfg_res.status, std::move(all_diagnostics));
    }
    const EventConfiguration& cfg = *cfg_res.value;

    auto map_res = loadEventRoleMapping(event_id);
    appendDiags(map_res.diagnostics);
    if (!map_res.ok()) {
        return Result<EventBundle>::failure(map_res.status, std::move(all_diagnostics));
    }
    const EventRoleMapping& role_map = *map_res.value;

    auto layout_res = loadTrackLayout(cfg.track_id, cfg.layout_version);
    appendDiags(layout_res.diagnostics);
    if (!layout_res.ok()) {
        return Result<EventBundle>::failure(layout_res.status, std::move(all_diagnostics));
    }
    const TrackLayoutVersion& layout = *layout_res.value;

    auto surface_res = loadTrackSurface(cfg.track_id, cfg.surface_version, cfg.layout_version);
    appendDiags(surface_res.diagnostics);
    if (!surface_res.ok()) {
        return Result<EventBundle>::failure(surface_res.status, std::move(all_diagnostics));
    }
    const TrackSurfaceProfile& surface = *surface_res.value;

    auto cat_res = loadCompoundCatalogue(cfg.season_year, role_map.spec_version);
    appendDiags(cat_res.diagnostics);
    if (!cat_res.ok()) {
        return Result<EventBundle>::failure(cat_res.status, std::move(all_diagnostics));
    }
    const PhysicalCompoundCatalogue& catalogue = *cat_res.value;

    std::vector<Diagnostic> cross_errors;
    auto recordCrossError = [&](const std::string& dataset,
                                const std::string& field_path,
                                const std::string& msg,
                                const std::string& expected,
                                const std::string& actual) {
        cross_errors.push_back(Diagnostic{
            Severity::Error,
            DiagnosticCode::CrossReferenceFailure,
            msg,
            dataset,
            "",
            field_path,
            expected,
            actual
        });
    };

    if (surface.track_id != layout.track_id || surface.layout_version != layout.layout_version) {
        recordCrossError(
            event_id,
            "/layout_version",
            "TrackSurfaceProfile (track_id, layout_version) does not match TrackLayoutVersion.",
            layout.track_id + "_v" + std::to_string(layout.layout_version),
            surface.track_id + "_v" + std::to_string(surface.layout_version));
    }

    if (role_map.event_id != cfg.event_id) {
        recordCrossError(
            event_id,
            "/event_id",
            "EventRoleMapping event_id does not match EventConfiguration event_id.",
            cfg.event_id,
            role_map.event_id);
    }

    if (role_map.season_year != cfg.season_year) {
        recordCrossError(
            event_id,
            "/season_year",
            "EventRoleMapping season_year does not match EventConfiguration season_year.",
            std::to_string(cfg.season_year),
            std::to_string(role_map.season_year));
    }

    if (role_map.is_hypothetical_override != cfg.is_hypothetical_override) {
        recordCrossError(
            event_id,
            "/is_hypothetical_override",
            "EventRoleMapping is_hypothetical_override disagrees with EventConfiguration.",
            cfg.is_hypothetical_override ? "true" : "false",
            role_map.is_hypothetical_override ? "true" : "false");
    }

    std::set<PhysicalCompoundId> cfg_nominated(
        cfg.nominated_compounds_set.begin(), cfg.nominated_compounds_set.end());
    std::set<PhysicalCompoundId> map_nominated(
        role_map.nominated_compounds_set.begin(), role_map.nominated_compounds_set.end());
    if (cfg_nominated != map_nominated) {
        recordCrossError(
            event_id,
            "/nominated_compounds_set",
            "EventRoleMapping nominated_compounds_set does not match EventConfiguration nominated_compounds_set.",
            "Matching 3-compound set N_e",
            "disjoint/mismatched nominated_compounds_set");
    }

    for (const auto& entry : role_map.mappings) {
        const PhysicalCompoundSpec* spec = catalogue.findCompound(entry.compound_id);
        if (spec == nullptr) {
            recordCrossError(
                event_id,
                "/mappings",
                std::string("Role ") + toString(entry.role_id) + " maps to compound " +
                    toString(entry.compound_id) + " which is absent from PhysicalCompoundCatalogue.",
                toString(entry.compound_id),
                "missing in catalogue");
        } else if (spec->hardness_rank != entry.hardness_rank) {
            recordCrossError(
                event_id,
                "/mappings",
                std::string("Role ") + toString(entry.role_id) + " hardness_rank disagrees with PhysicalCompoundCatalogue.",
                std::to_string(spec->hardness_rank),
                std::to_string(entry.hardness_rank));
        }
    }

    const double implied_distance_km =
        (static_cast<double>(cfg.race_distance_laps) * layout.lap_length_m) / 1000.0;
    if (std::fabs(cfg.scheduled_race_distance_km - implied_distance_km) > 0.5) {
        recordCrossError(
            event_id,
            "/scheduled_race_distance_km",
            "EventConfiguration scheduled_race_distance_km is inconsistent with race_distance_laps * TrackLayoutVersion.lap_length_m.",
            std::to_string(implied_distance_km) + " km (+/- 0.5 km)",
            std::to_string(cfg.scheduled_race_distance_km) + " km");
    }

    if (!cross_errors.empty()) {
        emitDiagnostics(cross_errors);
        appendDiags(cross_errors);
        return Result<EventBundle>::failure(LoadStatus::CrossReferenceFailed, std::move(all_diagnostics));
    }

    EventBundle bundle{
        std::move(*cfg_res.value),
        std::move(*map_res.value),
        std::move(*layout_res.value),
        std::move(*surface_res.value),
        std::move(*cat_res.value)
    };
    return Result<EventBundle>::success(std::move(bundle), std::move(all_diagnostics));
}

Result<void> DataAccessLayer::checkBaselineSimulationSufficiency(const EventBundle& bundle) const {
    std::vector<Diagnostic> issues;
    for (PhysicalCompoundId comp_id : bundle.event_config.nominated_compounds_set) {
        const PhysicalCompoundSpec* spec = bundle.compound_catalogue.findCompound(comp_id);
        const std::string comp_label = toString(comp_id);
        if (spec == nullptr) {
            issues.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Nominated compound '" + comp_label + "' is missing from loaded catalogue.",
                bundle.event_config.event_id,
                "",
                "/compounds/" + comp_label,
                "Calibrated PhysicalCompoundSpec",
                "missing"
            });
            continue;
        }
        if (!spec->pace_offset_ref_s.has_value()) {
            issues.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Nominated compound '" + comp_label + "' has uncalibrated (null) pace_offset_ref_s; required for Stage M0-M2 lap-time simulation.",
                bundle.event_config.event_id,
                "",
                "/compounds/" + comp_label + "/pace_offset_ref_s",
                "non-null double in [-2.0, 2.0]",
                "null"
            });
        }
        if (!spec->base_wear_rate_per_lap.has_value()) {
            issues.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Nominated compound '" + comp_label + "' has uncalibrated (null) base_wear_rate_per_lap; required for Stage M0-M2 tyre health update.",
                bundle.event_config.event_id,
                "",
                "/compounds/" + comp_label + "/base_wear_rate_per_lap",
                "non-null double in [0.005, 0.080]",
                "null"
            });
        }
        if (!spec->linear_deg_coeff_s.has_value()) {
            issues.push_back(Diagnostic{
                Severity::Error,
                DiagnosticCode::InsufficientDataForCapability,
                "Nominated compound '" + comp_label + "' has uncalibrated (null) linear_deg_coeff_s; required for Stage M0-M2 tyre degradation penalty.",
                bundle.event_config.event_id,
                "",
                "/compounds/" + comp_label + "/linear_deg_coeff_s",
                "non-null double in [0.5, 8.0]",
                "null"
            });
        }
    }

    if (!issues.empty()) {
        emitDiagnostics(issues);
        return Result<void>::failure(LoadStatus::InsufficientCapabilityData, std::move(issues));
    }
    return Result<void>::success();
}

} // namespace data_access
} // namespace f1_sim