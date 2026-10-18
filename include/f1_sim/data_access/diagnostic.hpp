#ifndef F1_SIM_DATA_ACCESS_DIAGNOSTIC_HPP
#define F1_SIM_DATA_ACCESS_DIAGNOSTIC_HPP

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace f1_sim {
namespace data_access {

/**
 * @brief Severity level for DAL and Engine-reported diagnostics.
 */
enum class Severity {
    Info,
    Warning,
    Error
};

/**
 * @brief Stable machine-readable diagnostic category codes.
 */
enum class DiagnosticCode {
    Ok = 0,
    InvalidRequestIdentifier,
    PathOutsideDataRoot,
    DatasetFileNotFound,
    FileReadPermissionError,
    MalformedJson,
    UnsupportedSchemaVersion,
    StructuralSchemaFailure,
    InvalidRangeOrEnum,
    CrossReferenceFailure,
    LegitimateUnknownValue,
    AssumedOrLowConfidenceValue,
    InsufficientDataForCapability,
    EngineReportedDiscrepancy,
    UnexpectedInternalFailure
};

/**
 * @brief High-level outcome status for a DAL request.
 */
enum class LoadStatus {
    Success = 0,
    InvalidRequest,
    PathOutsideRoot,
    FileNotFound,
    IoError,
    MalformedJson,
    UnsupportedSchemaVersion,
    SchemaValidationFailed,
    SemanticValidationFailed,
    CrossReferenceFailed,
    InsufficientCapabilityData,
    InternalError
};

/**
 * @brief Convert Severity enum to a stable string label.
 */
inline const char* toString(Severity severity) noexcept {
    switch (severity) {
        case Severity::Info:    return "INFO";
        case Severity::Warning: return "WARNING";
        case Severity::Error:   return "ERROR";
    }
    return "UNKNOWN_SEVERITY";
}

/**
 * @brief Convert DiagnosticCode enum to a stable machine-readable token.
 */
inline const char* toString(DiagnosticCode code) noexcept {
    switch (code) {
        case DiagnosticCode::Ok:
            return "OK";
        case DiagnosticCode::InvalidRequestIdentifier:
            return "ERR_INVALID_REQUEST_IDENTIFIER";
        case DiagnosticCode::PathOutsideDataRoot:
            return "ERR_PATH_OUTSIDE_DATA_ROOT";
        case DiagnosticCode::DatasetFileNotFound:
            return "ERR_DATASET_FILE_NOT_FOUND";
        case DiagnosticCode::FileReadPermissionError:
            return "ERR_FILE_READ_PERMISSION_ERROR";
        case DiagnosticCode::MalformedJson:
            return "ERR_MALFORMED_JSON";
        case DiagnosticCode::UnsupportedSchemaVersion:
            return "ERR_UNSUPPORTED_SCHEMA_VERSION";
        case DiagnosticCode::StructuralSchemaFailure:
            return "ERR_STRUCTURAL_SCHEMA_FAILURE";
        case DiagnosticCode::InvalidRangeOrEnum:
            return "ERR_INVALID_RANGE_OR_ENUM";
        case DiagnosticCode::CrossReferenceFailure:
            return "ERR_CROSS_REFERENCE_FAILURE";
        case DiagnosticCode::LegitimateUnknownValue:
            return "WARN_LEGITIMATE_UNKNOWN_VALUE";
        case DiagnosticCode::AssumedOrLowConfidenceValue:
            return "WARN_ASSUMED_OR_LOW_CONFIDENCE_VALUE";
        case DiagnosticCode::InsufficientDataForCapability:
            return "ERR_INSUFFICIENT_DATA_FOR_CAPABILITY";
        case DiagnosticCode::EngineReportedDiscrepancy:
            return "DIAG_ENGINE_REPORTED_DISCREPANCY";
        case DiagnosticCode::UnexpectedInternalFailure:
            return "ERR_UNEXPECTED_INTERNAL_FAILURE";
    }
    return "ERR_UNKNOWN_DIAGNOSTIC_CODE";
}

/**
 * @brief Convert LoadStatus enum to a stable string label.
 */
inline const char* toString(LoadStatus status) noexcept {
    switch (status) {
        case LoadStatus::Success:                    return "SUCCESS";
        case LoadStatus::InvalidRequest:             return "INVALID_REQUEST";
        case LoadStatus::PathOutsideRoot:            return "PATH_OUTSIDE_ROOT";
        case LoadStatus::FileNotFound:               return "FILE_NOT_FOUND";
        case LoadStatus::IoError:                    return "IO_ERROR";
        case LoadStatus::MalformedJson:              return "MALFORMED_JSON";
        case LoadStatus::UnsupportedSchemaVersion:   return "UNSUPPORTED_SCHEMA_VERSION";
        case LoadStatus::SchemaValidationFailed:     return "SCHEMA_VALIDATION_FAILED";
        case LoadStatus::SemanticValidationFailed:   return "SEMANTIC_VALIDATION_FAILED";
        case LoadStatus::CrossReferenceFailed:       return "CROSS_REFERENCE_FAILED";
        case LoadStatus::InsufficientCapabilityData: return "INSUFFICIENT_CAPABILITY_DATA";
        case LoadStatus::InternalError:              return "INTERNAL_ERROR";
    }
    return "UNKNOWN_STATUS";
}

/**
 * @brief Structured diagnostic item carrying context for errors, warnings, and
 *        Engine-reported runtime observations.
 */
struct Diagnostic {
    Severity severity = Severity::Error;
    DiagnosticCode code = DiagnosticCode::UnexpectedInternalFailure;
    std::string message;
    std::string dataset_id;    // Logical dataset identifier when known
    std::string relative_path; // Path relative to configured data root (never leaks machine-absolute paths)
    std::string field_path;    // JSON Pointer (e.g., "/compounds/0/pace_offset_ref_s") when known
    std::string expected;      // Optional expected constraint/value description
    std::string actual;        // Optional actual observed value description
};

/**
 * @brief Structured observation submitted by the Simulation Engine when it detects
 *        a runtime discrepancy or capability insufficiency.
 *
 * Reporting an observation records the discrepancy in the diagnostic sink and
 * returns a structured Diagnostic; it NEVER triggers file writes, recalibration,
 * default substitution, or automatic retries.
 */
struct EngineDiscrepancyReport {
    Severity severity = Severity::Warning;
    DiagnosticCode code = DiagnosticCode::EngineReportedDiscrepancy;
    std::string dataset_id;
    std::string relative_path;
    std::string field_path;
    std::string message;
    std::string expected;
    std::string actual;
};

/**
 * @brief Narrow sink interface for collecting diagnostics from both DAL load/validation
 *        operations and Simulation Engine runtime discrepancy reports.
 */
class DiagnosticSink {
public:
    virtual ~DiagnosticSink() = default;
    virtual void record(const Diagnostic& diagnostic) = 0;
};

/**
 * @brief Thread-safe, lightweight in-memory collector implementing DiagnosticSink.
 */
class InMemoryDiagnosticCollector final : public DiagnosticSink {
public:
    InMemoryDiagnosticCollector() = default;

    void record(const Diagnostic& diagnostic) override {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_.push_back(diagnostic);
    }

    std::vector<Diagnostic> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return diagnostics_;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return diagnostics_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::vector<Diagnostic> diagnostics_;
};

/**
 * @brief Structured result wrapper returned by all public DAL operations.
 */
template <typename T>
struct Result {
    LoadStatus status = LoadStatus::InternalError;
    std::optional<T> value = std::nullopt;
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept {
        return status == LoadStatus::Success && value.has_value();
    }

    explicit operator bool() const noexcept {
        return ok();
    }

    bool hasWarnings() const noexcept {
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Warning) {
                return true;
            }
        }
        return false;
    }

    bool hasErrors() const noexcept {
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Error) {
                return true;
            }
        }
        return false;
    }

    std::size_t warningCount() const noexcept {
        std::size_t count = 0;
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Warning) {
                ++count;
            }
        }
        return count;
    }

    std::size_t errorCount() const noexcept {
        std::size_t count = 0;
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Error) {
                ++count;
            }
        }
        return count;
    }

    static Result<T> success(T val, std::vector<Diagnostic> diags = {}) {
        Result<T> res;
        res.status = LoadStatus::Success;
        res.value = std::move(val);
        res.diagnostics = std::move(diags);
        return res;
    }

    static Result<T> failure(LoadStatus err_status, std::vector<Diagnostic> diags) {
        Result<T> res;
        res.status = err_status;
        res.value = std::nullopt;
        res.diagnostics = std::move(diags);
        return res;
    }
};

/**
 * @brief Specialization of Result<void> for validation/capability checks that
 *        do not produce a separate domain object.
 */
template <>
struct Result<void> {
    LoadStatus status = LoadStatus::InternalError;
    std::vector<Diagnostic> diagnostics;

    bool ok() const noexcept {
        return status == LoadStatus::Success;
    }

    explicit operator bool() const noexcept {
        return ok();
    }

    bool hasWarnings() const noexcept {
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Warning) {
                return true;
            }
        }
        return false;
    }

    bool hasErrors() const noexcept {
        for (const auto& d : diagnostics) {
            if (d.severity == Severity::Error) {
                return true;
            }
        }
        return false;
    }

    static Result<void> success(std::vector<Diagnostic> diags = {}) {
        Result<void> res;
        res.status = LoadStatus::Success;
        res.diagnostics = std::move(diags);
        return res;
    }

    static Result<void> failure(LoadStatus err_status, std::vector<Diagnostic> diags) {
        Result<void> res;
        res.status = err_status;
        res.diagnostics = std::move(diags);
        return res;
    }
};

} // namespace data_access
} // namespace f1_sim

#endif // F1_SIM_DATA_ACCESS_DIAGNOSTIC_HPP