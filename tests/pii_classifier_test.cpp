#include "pii/pii_classifier.hpp"

#include <cstdio>
#include <sstream>
#include <string>

using namespace wayblur;

static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Build a line from whitespace-separated text; every word gets a 50px-wide box.
static OcrLine make_line(const std::string& text) {
    OcrLine line;
    std::istringstream in(text);
    std::string w;
    int x = 10;
    while (in >> w) {
        line.words.push_back({w, {x, 100, 50, 20}});
        x += 60;
    }
    return line;
}

static int count_kind(const std::vector<PiiMatch>& ms, PiiKind k) {
    int n = 0;
    for (const auto& m : ms) if (m.kind == k) ++n;
    return n;
}

// Find the check digit that makes `prefix` + digit Verhoeff-valid.
static std::string with_verhoeff(const std::string& prefix) {
    for (char c = '0'; c <= '9'; ++c) {
        if (PiiClassifier::verhoeff_valid(prefix + c)) return prefix + c;
    }
    return prefix;
}

int main() {
    PiiClassifier cl;

    // --- checksum helpers ---
    CHECK(PiiClassifier::luhn_valid("4111111111111111"));
    CHECK(!PiiClassifier::luhn_valid("4111111111111112"));
    CHECK(PiiClassifier::verhoeff_valid("2363"));   // textbook Verhoeff example
    CHECK(!PiiClassifier::verhoeff_valid("2364"));

    // --- email ---
    {
        auto ms = cl.scan_line(make_line("contact: jane.doe+work@example.co.uk now"));
        CHECK(count_kind(ms, PiiKind::Email) == 1);
        CHECK(ms.size() == 1);
        // Box covers exactly the one email word (second word).
        CHECK(ms[0].box.x == 70 && ms[0].box.width == 50);
    }

    // --- credit card: spaced, spans 4 words, box is the union ---
    {
        auto ms = cl.scan_line(make_line("Card 4111 1111 1111 1111 exp"));
        CHECK(count_kind(ms, PiiKind::CreditCard) == 1);
        CHECK(ms.size() == 1);
        CHECK(ms[0].box.x == 70);
        CHECK(ms[0].box.width == 3 * 60 + 50);
    }
    // Luhn-invalid 16 digits must NOT be flagged as a card.
    {
        auto ms = cl.scan_line(make_line("ref 4111 1111 1111 1112 ok"));
        CHECK(count_kind(ms, PiiKind::CreditCard) == 0);
    }

    // --- aadhaar: valid checksum yes, invalid no ---
    {
        std::string good = with_verhoeff("23456789012");
        std::string spaced = good.substr(0, 4) + " " + good.substr(4, 4) + " " + good.substr(8, 4);
        auto ms = cl.scan_line(make_line("Aadhaar " + spaced));
        CHECK(count_kind(ms, PiiKind::Aadhaar) == 1);

        std::string bad = good;
        bad[11] = (bad[11] == '9') ? '0' : static_cast<char>(bad[11] + 1);
        std::string bad_spaced = bad.substr(0, 4) + " " + bad.substr(4, 4) + " " + bad.substr(8, 4);
        auto ms2 = cl.scan_line(make_line("Aadhaar " + bad_spaced));
        CHECK(count_kind(ms2, PiiKind::Aadhaar) == 0);
    }

    // --- PAN / SSN ---
    CHECK(count_kind(cl.scan_line(make_line("PAN ABCDE1234F")), PiiKind::PanCard) == 1);
    CHECK(count_kind(cl.scan_line(make_line("ssn 123-45-6789")), PiiKind::UsSsn) == 1);

    // --- phones ---
    CHECK(count_kind(cl.scan_line(make_line("call +91 98765 43210 today")), PiiKind::Phone) >= 1);
    CHECK(count_kind(cl.scan_line(make_line("call 9876543210")), PiiKind::Phone) == 1);
    CHECK(count_kind(cl.scan_line(make_line("tel +1 415 555 0132")), PiiKind::Phone) >= 1);

    // --- secrets ---
    CHECK(count_kind(cl.scan_line(make_line("key AKIAIOSFODNN7EXAMPLE")), PiiKind::Secret) == 1);
    CHECK(count_kind(cl.scan_line(make_line("token ghp_abcdefghijklmnopqrstuvwxyz0123456789")), PiiKind::Secret) == 1);
    CHECK(count_kind(cl.scan_line(make_line("sk-proj_abcdefghijklmnopqrstuvwx")), PiiKind::Secret) == 1);
    CHECK(count_kind(cl.scan_line(make_line("-----BEGIN RSA PRIVATE KEY-----")), PiiKind::Secret) == 1);

    // --- things that must stay clear ---
    CHECK(cl.scan_line(make_line("Meeting at 10:30 on 2026-11-24 in room 42")).empty());
    CHECK(cl.scan_line(make_line("Total: 1,234.56 USD (version 3.14.159)")).empty());
    CHECK(cl.scan_line(make_line("order ABC1234567890XYZ shipped")).empty());
    CHECK(cl.scan_line(make_line("The quick brown fox jumps over the lazy dog")).empty());
    CHECK(cl.scan_line(OcrLine{}).empty());

    if (g_failures == 0) std::puts("pii_classifier_test: all checks passed");
    return g_failures == 0 ? 0 : 1;
}
