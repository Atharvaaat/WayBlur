#include "pii/pii_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace wayblur {

namespace {

std::string digits_only(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return out;
}

struct Span {
    size_t begin;
    size_t end;   // exclusive
    PiiKind kind;
};

struct Pattern {
    PiiKind kind;
    std::regex re;
    // Optional validator on the matched text. Null means "accept".
    bool (*validate)(const std::string&);
};

bool validate_card(const std::string& m) {
    std::string d = digits_only(m);
    return d.size() >= 13 && d.size() <= 19 && PiiClassifier::luhn_valid(d);
}

bool validate_aadhaar(const std::string& m) {
    std::string d = digits_only(m);
    // Aadhaar numbers never start with 0 or 1.
    return d.size() == 12 && d[0] >= '2' && PiiClassifier::verhoeff_valid(d);
}

bool validate_phone(const std::string& m) {
    std::string d = digits_only(m);
    if (d.size() < 10 || d.size() > 13) return false;
    // A Luhn-valid 13-digit run is more likely a card; cards are matched separately.
    return true;
}

const std::vector<Pattern>& patterns() {
    static const std::vector<Pattern> p = [] {
        std::vector<Pattern> v;
        auto add = [&](PiiKind k, const char* re, bool (*val)(const std::string&)) {
            v.push_back({k, std::regex(re, std::regex::ECMAScript | std::regex::optimize), val});
        };

        add(PiiKind::Email,
            R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9\-]+(?:\.[A-Za-z0-9\-]+)*\.[A-Za-z]{2,})",
            nullptr);

        // 13-19 digits, optionally grouped by single spaces or dashes.
        add(PiiKind::CreditCard, R"((?:\d[ \-]?){12,18}\d)", validate_card);

        add(PiiKind::Aadhaar, R"([2-9]\d{3}[ \-]?\d{4}[ \-]?\d{4})", validate_aadhaar);

        add(PiiKind::PanCard, R"([A-Z]{5}\d{4}[A-Z])", nullptr);

        add(PiiKind::UsSsn, R"(\d{3}-\d{2}-\d{4})", nullptr);

        // International (+CC ...) or Indian 10-digit mobile (6-9 prefix) or grouped national numbers.
        add(PiiKind::Phone,
            R"((?:\+\d{1,3}[ \-.]?)?(?:\(?\d{2,5}\)?[ \-.]?)?\d{3,5}[ \-.]?\d{4,5})",
            validate_phone);

        add(PiiKind::Secret, R"(sk-[A-Za-z0-9_\-]{20,})", nullptr);
        add(PiiKind::Secret, R"(AKIA[0-9A-Z]{16})", nullptr);
        add(PiiKind::Secret, R"(gh[pousr]_[A-Za-z0-9]{30,})", nullptr);
        add(PiiKind::Secret, R"(xox[baprs]-[A-Za-z0-9\-]{10,})", nullptr);
        add(PiiKind::Secret, R"(AIza[0-9A-Za-z_\-]{35})", nullptr);
        add(PiiKind::Secret, R"(eyJ[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,})", nullptr);
        add(PiiKind::Secret, R"(-----BEGIN [A-Z ]*PRIVATE KEY-----)", nullptr);
        return v;
    }();
    return p;
}

} // namespace

const char* pii_kind_name(PiiKind kind) {
    switch (kind) {
        case PiiKind::Email: return "email";
        case PiiKind::Phone: return "phone";
        case PiiKind::CreditCard: return "credit-card";
        case PiiKind::Aadhaar: return "aadhaar";
        case PiiKind::PanCard: return "pan";
        case PiiKind::UsSsn: return "ssn";
        case PiiKind::Secret: return "secret";
    }
    return "unknown";
}

bool pii_kind_from_name(const std::string& name, PiiKind& out) {
    for (int i = 0; i <= static_cast<int>(PiiKind::Secret); ++i) {
        if (name == pii_kind_name(static_cast<PiiKind>(i))) { out = static_cast<PiiKind>(i); return true; }
    }
    return false;
}

bool PiiClassifier::luhn_valid(const std::string& digits) {
    if (digits.size() < 2) return false;
    int sum = 0;
    bool dbl = false;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (!std::isdigit(static_cast<unsigned char>(*it))) return false;
        int d = *it - '0';
        if (dbl) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        dbl = !dbl;
    }
    return sum % 10 == 0;
}

bool PiiClassifier::verhoeff_valid(const std::string& digits) {
    static const int d[10][10] = {
        {0,1,2,3,4,5,6,7,8,9}, {1,2,3,4,0,6,7,8,9,5}, {2,3,4,0,1,7,8,9,5,6},
        {3,4,0,1,2,8,9,5,6,7}, {4,0,1,2,3,9,5,6,7,8}, {5,9,8,7,6,0,4,3,2,1},
        {6,5,9,8,7,1,0,4,3,2}, {7,6,5,9,8,2,1,0,4,3}, {8,7,6,5,9,3,2,1,0,4},
        {9,8,7,6,5,4,3,2,1,0}};
    static const int p[8][10] = {
        {0,1,2,3,4,5,6,7,8,9}, {1,5,7,6,2,8,3,0,9,4}, {5,8,0,3,7,9,6,1,4,2},
        {8,9,1,6,0,4,3,5,2,7}, {9,4,5,3,1,2,6,8,7,0}, {4,2,8,6,5,7,3,9,0,1},
        {2,7,9,3,8,0,6,4,1,5}, {7,0,4,6,9,1,3,2,5,8}};

    if (digits.empty()) return false;
    int c = 0;
    size_t i = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it, ++i) {
        if (!std::isdigit(static_cast<unsigned char>(*it))) return false;
        c = d[c][p[i % 8][*it - '0']];
    }
    return c == 0;
}

std::vector<PiiMatch> PiiClassifier::scan_line(const OcrLine& line) const {
    std::vector<PiiMatch> out;
    if (line.words.empty()) return out;

    // Join words with single spaces and remember each word's character range.
    std::string text;
    std::vector<std::pair<size_t, size_t>> ranges;
    for (const auto& w : line.words) {
        if (!text.empty()) text.push_back(' ');
        size_t b = text.size();
        text += w.text;
        ranges.emplace_back(b, text.size());
    }

    std::vector<Span> spans;
    for (const auto& pat : patterns()) {
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pat.re);
             it != std::sregex_iterator(); ++it) {
            size_t b = static_cast<size_t>(it->position());
            size_t e = b + static_cast<size_t>(it->length());
            if (pat.validate && !pat.validate(it->str())) continue;

            // Skip matches glued into a longer alphanumeric token (e.g. an order id).
            bool left_glued = b > 0 && std::isalnum(static_cast<unsigned char>(text[b - 1]));
            bool right_glued = e < text.size() && std::isalnum(static_cast<unsigned char>(text[e]));
            if (pat.kind != PiiKind::Secret && pat.kind != PiiKind::Email &&
                (left_glued || right_glued)) {
                continue;
            }
            spans.push_back({b, e, pat.kind});
        }
    }

    // Prefer stronger classifications when spans overlap: card/aadhaar/etc. beat phone.
    auto strength = [](PiiKind k) { return k == PiiKind::Phone ? 0 : 1; };
    for (const auto& s : spans) {
        bool shadowed = false;
        for (const auto& o : spans) {
            if (&o == &s) continue;
            bool overlap = s.begin < o.end && o.begin < s.end;
            if (overlap && strength(o.kind) > strength(s.kind)) { shadowed = true; break; }
        }
        if (shadowed) continue;

        BoundingBox box;
        bool have = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        for (size_t i = 0; i < ranges.size(); ++i) {
            if (!(s.begin < ranges[i].second && ranges[i].first < s.end)) continue;
            const auto& wb = line.words[i].box;
            if (!have) {
                x0 = wb.x; y0 = wb.y; x1 = wb.x + wb.width; y1 = wb.y + wb.height;
                have = true;
            } else {
                x0 = std::min(x0, wb.x);
                y0 = std::min(y0, wb.y);
                x1 = std::max(x1, wb.x + wb.width);
                y1 = std::max(y1, wb.y + wb.height);
            }
        }
        if (!have) continue;
        box = {x0, y0, x1 - x0, y1 - y0};
        out.push_back({s.kind, box, text.substr(s.begin, s.end - s.begin)});
    }
    return out;
}

} // namespace wayblur
