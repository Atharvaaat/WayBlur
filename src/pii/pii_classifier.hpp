#ifndef WAYBLUR_PII_CLASSIFIER_HPP
#define WAYBLUR_PII_CLASSIFIER_HPP

#include "renderer/privacy_filter.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace wayblur {

enum class PiiKind {
    Email,
    Phone,
    CreditCard,
    Aadhaar,
    PanCard,
    UsSsn,
    Secret   // API keys, tokens, JWTs, private-key headers
};

const char* pii_kind_name(PiiKind kind);
bool pii_kind_from_name(const std::string& name, PiiKind& out);
constexpr uint32_t kAllPiiKinds = 0x7Fu;   // bits 0..6, one per PiiKind
constexpr uint32_t pii_kind_bit(PiiKind k) { return 1u << static_cast<unsigned>(k); }

// One word as produced by OCR, in frame pixel coordinates.
struct OcrWord {
    std::string text;
    BoundingBox box;
};

// One visual text line (words left-to-right).
struct OcrLine {
    std::vector<OcrWord> words;
};

struct PiiMatch {
    PiiKind kind;
    BoundingBox box;       // union of the word boxes covered by the match
    std::string excerpt;   // matched text (for logging/tests only, never persisted)
};

// Pure text classification — no OCR, no I/O. Safe to unit test.
class PiiClassifier {
public:
    // Returns every PII span found in the line, mapped back to word boxes.
    std::vector<PiiMatch> scan_line(const OcrLine& line) const;

    // Helpers exposed for tests.
    static bool luhn_valid(const std::string& digits);
    static bool verhoeff_valid(const std::string& digits);
};

} // namespace wayblur

#endif // WAYBLUR_PII_CLASSIFIER_HPP
