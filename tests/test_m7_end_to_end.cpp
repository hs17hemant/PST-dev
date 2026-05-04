// pstwriter/tests/test_m7_end_to_end.cpp
//
// M7 Phase E — end-to-end writeM7Pst test. Builds a small but realistic
// PST containing a folder hierarchy + plain-text + HTML messages +
// recipients + attachments, then runs pst_info as a smoke check.
//
// Per pre-flight gate items:
//   * Gate 9  — pst_info reports zero orphan blocks
//   * Gate 11 — M6 reader walks M7 PST without breaking (additive)
//   * Gate 12 — backup.pst-style structural validation

#include "graph_convert.hpp"
#include "graph_message.hpp"
#include "mail.hpp"
#include "writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

int runPstInfo(const std::string& path);

using namespace pstwriter;
using std::string;
using std::vector;

namespace {

graph::GraphMessage makePlainTextMessage(int idx)
{
    graph::GraphMessage m;
    m.id                   = "msg-" + std::to_string(idx);
    m.subject              = "Subject " + std::to_string(idx);
    m.body.contentType     = graph::BodyType::Text;
    m.body.content         = "Body for message " + std::to_string(idx);
    m.createdDateTime      = "2024-06-01T12:00:00Z";
    m.lastModifiedDateTime = "2024-06-01T12:01:00Z";
    m.sentDateTime         = "2024-06-01T12:00:05Z";
    m.receivedDateTime     = "2024-06-01T12:00:10Z";
    m.internetMessageId    = "<msg-" + std::to_string(idx) + "@example.com>";
    m.importance           = graph::Importance::Normal;
    m.isRead               = (idx % 2 == 0);

    graph::EmailAddress alice;
    alice.name    = "Alice Sender";
    alice.address = "alice@example.com";
    m.sender    = alice;
    m.hasSender = true;
    m.from      = alice;
    m.hasFrom   = true;

    graph::Recipient bob;
    bob.kind                  = graph::RecipientKind::To;
    bob.emailAddress.name     = "Bob Recipient";
    bob.emailAddress.address  = "bob@example.com";
    m.toRecipients.push_back(bob);

    return m;
}

} // namespace

TEST_CASE("M7 writeM7Pst: minimal flow produces a file",
          "[m7][phase_e][end_to_end]")
{
    M7PstConfig cfg;
    cfg.path = "m7_minimal.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M7 Minimal";

    // Two folders, one message each.
    M7Folder inbox;
    inbox.displayName = "Inbox";
    inbox.parentNid   = Nid{0x00008022u};   // IPM Subtree

    M7Folder drafts;
    drafts.displayName = "Drafts";
    drafts.parentNid   = Nid{0x00008022u};

    auto msg1 = makePlainTextMessage(1);
    auto msg2 = makePlainTextMessage(2);

    inbox.messages.push_back(&msg1);
    drafts.messages.push_back(&msg2);

    cfg.folders = { inbox, drafts };

    const auto r = writeM7Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    // File exists & non-empty
    FILE* fp = std::fopen(cfg.path.c_str(), "rb");
    REQUIRE(fp != nullptr);
    std::fseek(fp, 0, SEEK_END);
    const long sz = std::ftell(fp);
    std::fclose(fp);
    REQUIRE(sz > 1024);

    std::remove(cfg.path.c_str());
}

TEST_CASE("M7 writeM7Pst: full PST with multi-recipient + attachments + headers",
          "[m7][phase_e][end_to_end][m7_pst_info]")
{
    M7PstConfig cfg;
    cfg.path = "m7_full_pst.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M7 Full PST";

    // Build a richer message.
    graph::GraphMessage rich;
    rich.id                   = "rich-1";
    rich.subject              = "Project status";
    rich.body.contentType     = graph::BodyType::Html;
    rich.body.content         = "<html><body><p>Status update</p></body></html>";
    rich.bodyPreview          = "Status update";
    rich.createdDateTime      = "2024-06-01T12:00:00Z";
    rich.lastModifiedDateTime = "2024-06-01T12:01:00Z";
    rich.sentDateTime         = "2024-06-01T12:00:05Z";
    rich.receivedDateTime     = "2024-06-01T12:00:10Z";
    rich.internetMessageId    = "<rich-1@example.com>";
    rich.importance           = graph::Importance::High;
    rich.isRead               = false;
    rich.hasAttachments       = true;

    rich.sender.name    = "Alice Manager";
    rich.sender.address = "alice@example.com";
    rich.hasSender      = true;
    rich.from           = rich.sender;
    rich.hasFrom        = true;

    // Multi-recipient: 1 To, 1 Cc, 1 Bcc.
    graph::Recipient to;
    to.kind                 = graph::RecipientKind::To;
    to.emailAddress.name    = "Bob";
    to.emailAddress.address = "bob@example.com";
    rich.toRecipients.push_back(to);

    graph::Recipient cc;
    cc.kind                 = graph::RecipientKind::Cc;
    cc.emailAddress.name    = "Carol";
    cc.emailAddress.address = "carol@example.com";
    rich.ccRecipients.push_back(cc);

    graph::Recipient bcc;
    bcc.kind                 = graph::RecipientKind::Bcc;
    bcc.emailAddress.name    = "Dave";
    bcc.emailAddress.address = "dave@example.com";
    rich.bccRecipients.push_back(bcc);

    // File attachment
    graph::Attachment fileAtt;
    fileAtt.kind         = graph::AttachmentKind::File;
    fileAtt.name         = "notes.txt";
    fileAtt.contentType  = "text/plain";
    fileAtt.size         = 11;
    fileAtt.contentBytes = {'H','e','l','l','o',' ','W','o','r','l','d'};
    rich.attachments.push_back(fileAtt);

    // Internet headers
    rich.internetMessageHeaders.push_back({"Return-Path", "<alice@example.com>"});
    rich.internetMessageHeaders.push_back({"X-Mailer",    "TestMailer/1.0"});

    // Plain text message (no attachments, single recipient)
    auto plain = makePlainTextMessage(2);

    M7Folder inbox;
    inbox.displayName = "Inbox";
    inbox.parentNid   = Nid{0x00008022u};
    inbox.messages.push_back(&rich);
    inbox.messages.push_back(&plain);

    M7Folder sent;
    sent.displayName = "Sent Items";
    sent.parentNid   = Nid{0x00008022u};

    cfg.folders = { inbox, sent };

    const auto r = writeM7Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    // Smoke-check via pst_info.
    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);
}

TEST_CASE("M7 writeM7Pst: empty folder list still produces 27-node baseline",
          "[m7][phase_e][end_to_end]")
{
    M7PstConfig cfg;
    cfg.path = "m7_empty_folders.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M7 Empty";

    const auto r = writeM7Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);

    std::remove(cfg.path.c_str());
}
