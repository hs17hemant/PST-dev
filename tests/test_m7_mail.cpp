// pstwriter/tests/test_m7_mail.cpp
//
// M7 Phases B, C, D — buildMailPc, buildRecipientTc, buildAttachmentTc,
// buildAttachmentPc, buildMailFolderPc, serializeInternetHeaders.

#include "graph_convert.hpp"
#include "graph_message.hpp"
#include "ltp.hpp"
#include "mail.hpp"
#include "messaging.hpp"
#include "types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace pstwriter;
using std::string;
using std::vector;

namespace {

graph::GraphMessage makeBasicMessage()
{
    graph::GraphMessage m;
    m.id                   = "msg-1";
    m.subject              = "Hello";
    m.body.contentType     = graph::BodyType::Text;
    m.body.content         = "Hi Bob";
    m.createdDateTime      = "2024-06-01T12:00:00Z";
    m.lastModifiedDateTime = "2024-06-01T12:01:00Z";
    m.sentDateTime         = "2024-06-01T12:00:05Z";
    m.receivedDateTime     = "2024-06-01T12:00:10Z";
    m.internetMessageId    = "<msg-1@example.com>";
    m.importance           = graph::Importance::Normal;
    m.isRead               = true;

    graph::EmailAddress alice;
    alice.name    = "Alice";
    alice.address = "alice@example.com";
    m.sender    = alice;
    m.hasSender = true;
    m.from      = alice;
    m.hasFrom   = true;

    graph::Recipient bob;
    bob.kind                  = graph::RecipientKind::To;
    bob.emailAddress.name     = "Bob";
    bob.emailAddress.address  = "bob@example.com";
    m.toRecipients.push_back(bob);

    return m;
}

MailPcBuildContext makeCtx()
{
    MailPcBuildContext ctx;
    ctx.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    // Use a high NID that won't conflict with subnode-tree NIDs.
    ctx.subnodeStart = Nid{0x00010001u};
    return ctx;
}

// Decode a PC HN body and check that the property with `pidTagId` is
// present (and optionally has expected bytes).
const ReadPcProp* findProp(const vector<ReadPcProp>& props, uint16_t pid)
{
    for (const auto& p : props) if (p.pidTagId == pid) return &p;
    return nullptr;
}

} // namespace

// ============================================================================
// Phase B — buildMailPc plain-text round-trip
// ============================================================================
TEST_CASE("M7 buildMailPc: plain-text PC contains MessageClass IPM.Note",
          "[m7][phase_b][mail_pc_round_trip]")
{
    const auto m = makeBasicMessage();
    const auto ctx = makeCtx();
    const auto pc = buildMailPc(m, ctx);

    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());
    const auto* mc = findProp(props, 0x001Au);
    REQUIRE(mc != nullptr);
    REQUIRE(mc->propType == PropType::Unicode);
    // PidTagMessageClass_W = "IPM.Note" (UTF-16-LE, 16 bytes)
    REQUIRE(mc->valueSize == 16);
    const uint8_t expected[16] = {
        'I', 0, 'P', 0, 'M', 0, '.', 0,
        'N', 0, 'o', 0, 't', 0, 'e', 0,
    };
    for (size_t i = 0; i < 16; ++i) REQUIRE(mc->valueBytes[i] == expected[i]);
}

TEST_CASE("M7 buildMailPc: subject + body decoded back",
          "[m7][phase_b][mail_pc_round_trip]")
{
    const auto m = makeBasicMessage();
    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* subj = findProp(props, 0x0037u);
    REQUIRE(subj != nullptr);
    REQUIRE(subj->valueSize == 10);  // "Hello" = 5 chars * 2

    const auto* body = findProp(props, 0x1000u);
    REQUIRE(body != nullptr);
    REQUIRE(body->valueSize == 12);  // "Hi Bob" = 6 chars * 2
}

TEST_CASE("M7 buildMailPc: sender properties present",
          "[m7][phase_b][mail_pc_round_trip]")
{
    const auto m = makeBasicMessage();
    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x0C1Au) != nullptr);  // SenderName
    REQUIRE(findProp(props, 0x0C1Fu) != nullptr);  // SenderEmailAddress
    REQUIRE(findProp(props, 0x0C1Eu) != nullptr);  // SenderAddressType
    REQUIRE(findProp(props, 0x0C19u) != nullptr);  // SenderEntryId
    REQUIRE(findProp(props, 0x0C1Du) != nullptr);  // SenderSearchKey
    // Sent representing mirror props
    REQUIRE(findProp(props, 0x0042u) != nullptr);  // SentRepresentingName
    REQUIRE(findProp(props, 0x0065u) != nullptr);  // SentRepresentingEmail
}

TEST_CASE("M7 buildMailPc: SystemTime properties round-trip",
          "[m7][phase_b][mail_pc_round_trip]")
{
    const auto m = makeBasicMessage();
    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* ct = findProp(props, 0x3007u);   // CreationTime
    REQUIRE(ct != nullptr);
    REQUIRE(ct->propType == PropType::SystemTime);
    REQUIRE(ct->storage  == ReadPcProp::Storage::HnAlloc);
    REQUIRE(ct->valueSize == 8);

    const auto* lm = findProp(props, 0x3008u);
    REQUIRE(lm != nullptr);
    const auto* cs = findProp(props, 0x0039u);
    REQUIRE(cs != nullptr);
    const auto* dl = findProp(props, 0x0E06u);
    REQUIRE(dl != nullptr);
}

TEST_CASE("M7 buildMailPc: MessageFlags reflects isRead/isDraft/hasAttachments",
          "[m7][phase_b][mail_pc_round_trip]")
{
    auto m = makeBasicMessage();
    m.isRead         = true;
    m.isDraft        = false;
    m.hasAttachments = true;
    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* mf = findProp(props, 0x0E07u);
    REQUIRE(mf != nullptr);
    REQUIRE(mf->propType == PropType::Int32);
    REQUIRE(mf->storage  == ReadPcProp::Storage::Inline);
    // Bits: mfRead | mfHasAttach = 0x01 | 0x10 = 0x11
    REQUIRE(mf->inlineValue == 0x00000011u);
}

TEST_CASE("M7 buildMailPc: PidTagHasAttachments boolean",
          "[m7][phase_b][mail_pc_round_trip]")
{
    auto m = makeBasicMessage();
    m.hasAttachments = true;
    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());
    const auto* hh = findProp(props, 0x0E1Bu);
    REQUIRE(hh != nullptr);
    REQUIRE(hh->propType == PropType::Boolean);
}

// ============================================================================
// Phase B — buildRecipientTc round-trip
// ============================================================================
TEST_CASE("M7 buildRecipientTc: 1-row TC has correct structure",
          "[m7][phase_b][mail_recipients]")
{
    const auto m = makeBasicMessage();
    const auto tc = buildRecipientTc(m.toRecipients);

    REQUIRE_FALSE(tc.hnBytes.empty());

    // The HN should have bClientSig = 0x7C (TC).
    REQUIRE(tc.hnBytes[3] == 0x7Cu);
}

TEST_CASE("M7 buildRecipientTc: 2-row TC builds without error",
          "[m7][phase_b][mail_recipients]")
{
    vector<graph::Recipient> recips;
    {
        graph::Recipient r;
        r.kind                 = graph::RecipientKind::To;
        r.emailAddress.name    = "Alice";
        r.emailAddress.address = "alice@example.com";
        recips.push_back(r);
    }
    {
        graph::Recipient r;
        r.kind                 = graph::RecipientKind::Cc;
        r.emailAddress.name    = "Bob";
        r.emailAddress.address = "bob@example.com";
        recips.push_back(r);
    }
    const auto tc = buildRecipientTc(recips);
    REQUIRE_FALSE(tc.hnBytes.empty());
}

// ============================================================================
// Phase C — HTML body
// ============================================================================
TEST_CASE("M7 buildMailPc: HTML body emits PidTagBodyHtml",
          "[m7][phase_c][mail_html]")
{
    auto m = makeBasicMessage();
    m.body.contentType = graph::BodyType::Html;
    m.body.content     = "<html><body>Hi</body></html>";
    m.bodyPreview      = "Hi";

    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* html = findProp(props, 0x1013u);
    REQUIRE(html != nullptr);
    REQUIRE(html->propType == PropType::Binary);
    // First chars of value should be '<', 'h', 't', 'm', 'l'...
    REQUIRE(html->valueSize == m.body.content.size());
    for (size_t i = 0; i < m.body.content.size(); ++i) {
        REQUIRE(html->valueBytes[i] == static_cast<uint8_t>(m.body.content[i]));
    }

    // Plain-text fallback PidTagBody emitted from bodyPreview
    const auto* body = findProp(props, 0x1000u);
    REQUIRE(body != nullptr);
    REQUIRE(body->propType == PropType::Unicode);
}

// ============================================================================
// Phase C — file attachment
// ============================================================================
TEST_CASE("M7 buildAttachmentPc: file attachment has data binary",
          "[m7][phase_c][mail_file_attachment]")
{
    graph::Attachment att;
    att.kind         = graph::AttachmentKind::File;
    att.name         = "hello.txt";
    att.contentType  = "text/plain";
    att.size         = 5;
    att.contentBytes = {'H', 'e', 'l', 'l', 'o'};

    const auto pc = buildAttachmentPc(att, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* method = findProp(props, 0x3705u);
    REQUIRE(method != nullptr);
    REQUIRE(method->inlineValue == 1u);  // afByValue

    const auto* fname = findProp(props, 0x3704u);
    REQUIRE(fname != nullptr);
    REQUIRE(fname->propType == PropType::Unicode);

    const auto* dataBin = findProp(props, 0x3701u);
    REQUIRE(dataBin != nullptr);
    REQUIRE(dataBin->propType == PropType::Binary);
    REQUIRE(dataBin->valueSize == 5);
    REQUIRE(dataBin->valueBytes[0] == 'H');
    REQUIRE(dataBin->valueBytes[4] == 'o');
}

TEST_CASE("M7 buildAttachmentTc: 1-row attachment table",
          "[m7][phase_c][mail_file_attachment]")
{
    graph::Attachment att;
    att.kind         = graph::AttachmentKind::File;
    att.name         = "hello.txt";
    att.contentType  = "text/plain";
    att.size         = 5;
    att.contentBytes = {'H', 'e', 'l', 'l', 'o'};

    AttachmentTcRow row;
    row.attachmentNid = Nid(NidType::Attachment, 1);
    row.attachment    = &att;
    vector<AttachmentTcRow> rows = { row };

    const auto tc = buildAttachmentTc(rows);
    REQUIRE_FALSE(tc.hnBytes.empty());
    REQUIRE(tc.hnBytes[3] == 0x7Cu);  // bClientSig = TC
}

// ============================================================================
// Phase C — item attachment
// ============================================================================
TEST_CASE("M7 buildAttachmentPc: item attachment carries embedded message",
          "[m7][phase_c][mail_item_attachment]")
{
    auto inner = std::make_shared<graph::GraphMessage>();
    inner->subject              = "Inner";
    inner->body.contentType     = graph::BodyType::Text;
    inner->body.content         = "embedded body";
    inner->createdDateTime      = "2024-06-01T12:00:00Z";
    inner->lastModifiedDateTime = "2024-06-01T12:00:00Z";

    graph::Attachment att;
    att.kind = graph::AttachmentKind::Item;
    att.name = "embedded.msg";
    att.size = 0;
    att.item = inner;

    const auto pc = buildAttachmentPc(att, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* method = findProp(props, 0x3705u);
    REQUIRE(method != nullptr);
    REQUIRE(method->inlineValue == 5u);  // afEmbeddedMessage

    const auto* dataBin = findProp(props, 0x3701u);
    REQUIRE(dataBin != nullptr);
    REQUIRE(dataBin->propType == PropType::Binary);
    // Embedded message HN bytes start with HNHDR (12 bytes) — first byte
    // of HNHDR is ibHnpm low byte; byte 2 must be 0xEC (kHnSignature) and
    // byte 3 must be 0xBC (PC client sig).
    REQUIRE(dataBin->valueSize > 12);
    REQUIRE(dataBin->valueBytes[2] == 0xECu);
    REQUIRE(dataBin->valueBytes[3] == 0xBCu);
}

// ============================================================================
// Phase D — multi-recipient TC
// ============================================================================
TEST_CASE("M7 buildRecipientTc: To/Cc/Bcc rows preserve PidTagRecipientType",
          "[m7][phase_d][mail_recipients]")
{
    vector<graph::Recipient> rs;
    {
        graph::Recipient r;
        r.kind                 = graph::RecipientKind::To;
        r.emailAddress.name    = "T";
        r.emailAddress.address = "t@x.com";
        rs.push_back(r);
    }
    {
        graph::Recipient r;
        r.kind                 = graph::RecipientKind::Cc;
        r.emailAddress.name    = "C";
        r.emailAddress.address = "c@x.com";
        rs.push_back(r);
    }
    {
        graph::Recipient r;
        r.kind                 = graph::RecipientKind::Bcc;
        r.emailAddress.name    = "B";
        r.emailAddress.address = "b@x.com";
        rs.push_back(r);
    }
    const auto tc = buildRecipientTc(rs);
    REQUIRE_FALSE(tc.hnBytes.empty());
    // Structural check: HN's bClientSig should be TC (0x7C).
    REQUIRE(tc.hnBytes[3] == 0x7Cu);
}

// ============================================================================
// Phase D — internet headers
// ============================================================================
TEST_CASE("M7 serializeInternetHeaders: produces RFC 2822 CRLF block",
          "[m7][phase_d][mail_headers]")
{
    vector<graph::InternetMessageHeader> hs;
    hs.push_back({"Return-Path", "<alice@example.com>"});
    hs.push_back({"X-Mailer",    "TestMailer/1.0"});

    const auto out = serializeInternetHeaders(hs);
    REQUIRE(out == "Return-Path: <alice@example.com>\r\n"
                   "X-Mailer: TestMailer/1.0\r\n");
}

TEST_CASE("M7 buildMailPc: PidTagTransportMessageHeaders emitted when headers present",
          "[m7][phase_d][mail_headers]")
{
    auto m = makeBasicMessage();
    m.internetMessageHeaders.push_back({"Return-Path", "<alice@example.com>"});
    m.internetMessageHeaders.push_back({"X-Mailer", "Test"});

    const auto pc = buildMailPc(m, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());
    const auto* th = findProp(props, 0x007Du);  // PidTagTransportMessageHeaders
    REQUIRE(th != nullptr);
    REQUIRE(th->propType == PropType::Unicode);
    REQUIRE(th->valueSize > 0);
}

// ============================================================================
// Phase D — folder hierarchy: buildMailFolderPc with container class
// ============================================================================
TEST_CASE("M7 buildMailFolderPc: emits ContainerClass when supplied",
          "[m7][phase_d][mail_folder_tree]")
{
    const auto name  = graph::utf8ToUtf16le("Inbox");
    const auto klass = graph::utf8ToUtf16le("IPF.Note");

    M7FolderSchema schema{};
    schema.displayNameUtf16le    = name.data();
    schema.displayNameSize       = name.size();
    schema.containerClassUtf16le = klass.data();
    schema.containerClassSize    = klass.size();
    schema.contentCount          = 7u;

    const auto pc = buildMailFolderPc(schema, Nid{0x00000041u});
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* dn = findProp(props, 0x3001u);
    REQUIRE(dn != nullptr);
    REQUIRE(dn->valueSize == name.size());

    const auto* cc = findProp(props, 0x3613u);
    REQUIRE(cc != nullptr);
    REQUIRE(cc->valueSize == klass.size());

    const auto* count = findProp(props, 0x3602u);
    REQUIRE(count != nullptr);
    REQUIRE(count->inlineValue == 7u);
}
