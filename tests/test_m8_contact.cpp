// pstwriter/tests/test_m8_contact.cpp
//
// M8 Phases A, B, C — Graph contact JSON parser, buildContactPc,
// writeM8Pst end-to-end.

#include "contact.hpp"
#include "graph_contact.hpp"
#include "graph_convert.hpp"
#include "ltp.hpp"
#include "mail.hpp"
#include "types.hpp"
#include "writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

int runPstInfo(const std::string& path);

using namespace pstwriter;
using std::string;
using std::vector;

namespace {

const ReadPcProp* findProp(const vector<ReadPcProp>& props, uint16_t pid)
{
    for (const auto& p : props) if (p.pidTagId == pid) return &p;
    return nullptr;
}

graph::GraphContact makeBasicContact()
{
    graph::GraphContact c;
    c.id                   = "contact-1";
    c.displayName          = "Alice Manager";
    c.givenName            = "Alice";
    c.surname              = "Manager";
    c.title                = "Dr.";
    c.jobTitle             = "Engineering Manager";
    c.companyName          = "Example Corp";
    c.department           = "Engineering";
    c.officeLocation       = "Building 1";
    c.businessPhones       = { "+1-555-0100" };
    c.mobilePhone          = "+1-555-0101";
    c.homePhones           = { "+1-555-0102" };
    c.businessAddress.street          = "1 Main St";
    c.businessAddress.city            = "Seattle";
    c.businessAddress.state           = "WA";
    c.businessAddress.postalCode      = "98101";
    c.businessAddress.countryOrRegion = "USA";
    c.createdDateTime      = "2024-06-01T12:00:00Z";
    c.lastModifiedDateTime = "2024-06-01T12:01:00Z";
    c.birthday             = "1985-03-15T00:00:00Z";

    graph::EmailAddress ea;
    ea.name    = "Alice Manager";
    ea.address = "alice@example.com";
    c.emailAddresses.push_back(ea);

    return c;
}

MailPcBuildContext makeCtx()
{
    MailPcBuildContext ctx;
    ctx.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    ctx.subnodeStart = Nid{0x00010001u};
    return ctx;
}

} // namespace

// ============================================================================
// Phase A — JSON parser
// ============================================================================
TEST_CASE("M8 parseGraphContact: basic scalar fields",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "id": "contact-1",
        "displayName": "Alice Manager",
        "givenName": "Alice",
        "surname": "Manager",
        "title": "Dr.",
        "jobTitle": "Engineering Manager",
        "companyName": "Example Corp",
        "createdDateTime": "2024-06-01T12:00:00Z"
    })";
    const auto c = graph::parseGraphContact(json);
    REQUIRE(c.id == "contact-1");
    REQUIRE(c.displayName == "Alice Manager");
    REQUIRE(c.givenName == "Alice");
    REQUIRE(c.surname == "Manager");
    REQUIRE(c.title == "Dr.");
    REQUIRE(c.jobTitle == "Engineering Manager");
    REQUIRE(c.companyName == "Example Corp");
    REQUIRE(c.createdDateTime == "2024-06-01T12:00:00Z");
}

TEST_CASE("M8 parseGraphContact: emailAddresses array",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "displayName": "Alice",
        "emailAddresses": [
            {"name": "Alice", "address": "alice@example.com"},
            {"name": "Alice", "address": "alice.other@example.com"}
        ]
    })";
    const auto c = graph::parseGraphContact(json);
    REQUIRE(c.emailAddresses.size() == 2);
    REQUIRE(c.emailAddresses[0].address == "alice@example.com");
    REQUIRE(c.emailAddresses[1].address == "alice.other@example.com");
}

TEST_CASE("M8 parseGraphContact: phone arrays + mobilePhone scalar",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "displayName": "Alice",
        "businessPhones": ["+1-555-0100", "+1-555-0103"],
        "mobilePhone": "+1-555-0101",
        "homePhones": []
    })";
    const auto c = graph::parseGraphContact(json);
    REQUIRE(c.businessPhones.size() == 2);
    REQUIRE(c.businessPhones[0] == "+1-555-0100");
    REQUIRE(c.mobilePhone == "+1-555-0101");
    REQUIRE(c.homePhones.empty());
}

TEST_CASE("M8 parseGraphContact: physicalAddress sub-objects",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "displayName": "Alice",
        "businessAddress": {
            "street": "1 Main St",
            "city": "Seattle",
            "state": "WA",
            "postalCode": "98101",
            "countryOrRegion": "USA"
        },
        "homeAddress": {
            "street": "100 Home Ln",
            "city": "Bellevue",
            "state": "WA"
        }
    })";
    const auto c = graph::parseGraphContact(json);
    REQUIRE(c.businessAddress.street == "1 Main St");
    REQUIRE(c.businessAddress.city == "Seattle");
    REQUIRE(c.businessAddress.countryOrRegion == "USA");
    REQUIRE(c.homeAddress.street == "100 Home Ln");
    REQUIRE(c.homeAddress.postalCode.empty());
}

TEST_CASE("M8 parseGraphContact: tolerates unknown fields",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "displayName": "x",
        "futureField": {"nested": [1,2,3]},
        "anotherUnknown": "value"
    })";
    auto c = graph::parseGraphContact(json);
    REQUIRE(c.displayName == "x");
}

TEST_CASE("M8 parseGraphContactList: array form",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"([
        {"displayName": "A"},
        {"displayName": "B"}
    ])";
    auto v = graph::parseGraphContactList(json);
    REQUIRE(v.size() == 2);
    REQUIRE(v[0].displayName == "A");
    REQUIRE(v[1].displayName == "B");
}

TEST_CASE("M8 parseGraphContactList: {value:[...]} envelope",
          "[m8][phase_a][graph_contact_parser]")
{
    const string json = R"({
        "@odata.context": "...",
        "value": [
            {"displayName": "A"},
            {"displayName": "B"},
            {"displayName": "C"}
        ]
    })";
    auto v = graph::parseGraphContactList(json);
    REQUIRE(v.size() == 3);
    REQUIRE(v[2].displayName == "C");
}

// ============================================================================
// Phase B — buildContactPc
// ============================================================================
TEST_CASE("M8 buildContactPc: emits MessageClass IPM.Contact",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* mc = findProp(props, 0x001Au);
    REQUIRE(mc != nullptr);
    REQUIRE(mc->propType == PropType::Unicode);
    // "IPM.Contact" = 11 chars * 2 = 22 bytes
    REQUIRE(mc->valueSize == 22);
}

TEST_CASE("M8 buildContactPc: name fields populated",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x3001u) != nullptr); // DisplayName
    REQUIRE(findProp(props, 0x3A06u) != nullptr); // GivenName
    REQUIRE(findProp(props, 0x3A11u) != nullptr); // Surname
    REQUIRE(findProp(props, 0x3A45u) != nullptr); // DisplayNamePrefix (Graph 'title')
    REQUIRE(findProp(props, 0x3A17u) != nullptr); // JobTitle
}

TEST_CASE("M8 buildContactPc: company / department / office populated",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x3A16u) != nullptr); // CompanyName
    REQUIRE(findProp(props, 0x3A18u) != nullptr); // DepartmentName
    REQUIRE(findProp(props, 0x3A19u) != nullptr); // OfficeLocation
}

TEST_CASE("M8 buildContactPc: phones populated",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x3A08u) != nullptr); // BusinessTelephone
    REQUIRE(findProp(props, 0x3A09u) != nullptr); // HomeTelephone
    REQUIRE(findProp(props, 0x3A1Cu) != nullptr); // MobileTelephone
}

TEST_CASE("M8 buildContactPc: business address populated",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x3A29u) != nullptr); // BusinessAddrStreet
    REQUIRE(findProp(props, 0x3A27u) != nullptr); // BusinessAddrCity
    REQUIRE(findProp(props, 0x3A28u) != nullptr); // BusinessAddrState
    REQUIRE(findProp(props, 0x3A2Au) != nullptr); // BusinessAddrPostalCode
    REQUIRE(findProp(props, 0x3A26u) != nullptr); // BusinessAddrCountry
    REQUIRE(findProp(props, 0x3A15u) != nullptr); // PostalAddress (concatenated)
}

TEST_CASE("M8 buildContactPc: birthday SystemTime",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* bd = findProp(props, 0x3A42u);
    REQUIRE(bd != nullptr);
    REQUIRE(bd->propType == PropType::SystemTime);
    REQUIRE(bd->valueSize == 8);
}

TEST_CASE("M8 buildContactPc: email address populated",
          "[m8][phase_b][contact_pc_round_trip]")
{
    const auto c  = makeBasicContact();
    const auto pc = buildContactPc(c, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x3003u) != nullptr); // EmailAddress
    REQUIRE(findProp(props, 0x3002u) != nullptr); // AddressType ("SMTP")
}

TEST_CASE("M8 buildContactPc: minimal contact (only givenName + surname) builds",
          "[m8][phase_b][contact_pc_round_trip]")
{
    graph::GraphContact c;
    c.givenName = "Bob";
    c.surname   = "Builder";

    const auto pc = buildContactPc(c, makeCtx());
    REQUIRE_FALSE(pc.hnBytes.empty());

    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());
    const auto* dn = findProp(props, 0x3001u);
    REQUIRE(dn != nullptr);
    // Composed displayName = "Bob Builder"
    REQUIRE(dn->valueSize == 22);  // 11 chars * 2
}

// ============================================================================
// Phase C — writeM8Pst end-to-end
// ============================================================================
TEST_CASE("M8 writeM8Pst: minimal flow produces a file",
          "[m8][phase_c][end_to_end]")
{
    M8PstConfig cfg;
    cfg.path = "m8_minimal.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M8 Minimal";

    M8ContactFolder folder;
    folder.displayName = "Contacts";
    folder.parentNid   = Nid{0x00008022u};

    auto contact = makeBasicContact();
    folder.contacts.push_back(&contact);

    cfg.folders = { folder };

    const auto r = writeM8Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    FILE* fp = std::fopen(cfg.path.c_str(), "rb");
    REQUIRE(fp != nullptr);
    std::fseek(fp, 0, SEEK_END);
    const long sz = std::ftell(fp);
    std::fclose(fp);
    REQUIRE(sz > 1024);

    std::remove(cfg.path.c_str());
}

TEST_CASE("M8 writeM8Pst: full PST with multiple contacts in 2 folders",
          "[m8][phase_c][end_to_end][m8_pst_info]")
{
    M8PstConfig cfg;
    cfg.path = "m8_contacts.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M8 Contacts PST";

    auto alice = makeBasicContact();

    graph::GraphContact bob;
    bob.id          = "contact-2";
    bob.displayName = "Bob Builder";
    bob.givenName   = "Bob";
    bob.surname     = "Builder";
    bob.companyName = "BuildCorp";
    bob.businessPhones = { "+1-555-0200" };

    graph::GraphContact carol;
    carol.id          = "contact-3";
    carol.givenName   = "Carol";
    carol.surname     = "Customer";
    carol.companyName = "CustomerCo";

    M8ContactFolder primary;
    primary.displayName = "Contacts";
    primary.parentNid   = Nid{0x00008022u};
    primary.contacts.push_back(&alice);
    primary.contacts.push_back(&bob);

    M8ContactFolder personal;
    personal.displayName = "Personal Contacts";
    personal.parentNid   = Nid{0x00008022u};
    personal.contacts.push_back(&carol);

    cfg.folders = { primary, personal };

    const auto r = writeM8Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);
}

TEST_CASE("M8 writeM8Pst: empty folder list still produces baseline PST",
          "[m8][phase_c][end_to_end]")
{
    M8PstConfig cfg;
    cfg.path = "m8_empty.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M8 Empty";

    const auto r = writeM8Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);

    std::remove(cfg.path.c_str());
}
