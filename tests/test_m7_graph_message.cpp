// pstwriter/tests/test_m7_graph_message.cpp
//
// M7 Phase A — Graph message JSON parser tests. Targets gate item 1
// (parse all 32+ Graph fields).

#include "graph_message.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace pstwriter::graph;

namespace {

// A realistic Graph "message" JSON sample exercising the major field
// surface. Field selection mirrors the [MS-Graph] resource doc and the
// M7 mapping table.
const char kSampleMessageJson[] = R"JSON({
  "@odata.context": "https://graph.microsoft.com/v1.0/$metadata#users('me')/messages/$entity",
  "id": "AAMkADExMzZkOWRiLTU0NjctNDI4ZS04MDU2LTk2N2E1NzMyZDU2",
  "internetMessageId": "<abc-123@example.com>",
  "conversationId": "AAQkADExMzZkOWRiLTU0NjctNDI4ZS04MDU2LTk2N2E1NzMyZDU2",
  "conversationIndex": "AQHX1AAB",
  "subject": "Hello world",
  "bodyPreview": "Hello world preview",
  "body": {
    "contentType": "text",
    "content": "Hello world body"
  },
  "createdDateTime": "2024-01-15T10:30:00Z",
  "lastModifiedDateTime": "2024-01-15T10:31:00Z",
  "sentDateTime": "2024-01-15T10:30:05Z",
  "receivedDateTime": "2024-01-15T10:30:10Z",
  "sender": {
    "emailAddress": { "name": "Alice Sender", "address": "alice@example.com" }
  },
  "from": {
    "emailAddress": { "name": "Alice Sender", "address": "alice@example.com" }
  },
  "toRecipients": [
    { "emailAddress": { "name": "Bob",   "address": "bob@example.com" } },
    { "emailAddress": { "name": "Carol", "address": "carol@example.com" } }
  ],
  "ccRecipients": [
    { "emailAddress": { "name": "Dave",  "address": "dave@example.com" } }
  ],
  "bccRecipients": [],
  "replyTo": [
    { "emailAddress": { "name": "Alice Sender", "address": "alice@example.com" } }
  ],
  "isRead": true,
  "isDraft": false,
  "hasAttachments": true,
  "importance": "high",
  "flag": { "flagStatus": "flagged" },
  "categories": ["Work", "Urgent"],
  "internetMessageHeaders": [
    { "name": "Return-Path", "value": "<alice@example.com>" },
    { "name": "X-Mailer",    "value": "TestMailer/1.0" }
  ],
  "attachments": [
    {
      "@odata.type": "#microsoft.graph.fileAttachment",
      "name": "hello.txt",
      "contentType": "text/plain",
      "contentId": null,
      "isInline": false,
      "size": 5,
      "contentBytes": "SGVsbG8="
    }
  ],
  "parentFolderId": "AAMkADExMzZkOWRiLTU0NjctNDI4ZS04MDU2LTk2N2E1NzMyZDU2"
})JSON";

} // namespace

TEST_CASE("M7 parseGraphMessage: top-level scalar fields",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);

    REQUIRE(m.id == "AAMkADExMzZkOWRiLTU0NjctNDI4ZS04MDU2LTk2N2E1NzMyZDU2");
    REQUIRE(m.internetMessageId == "<abc-123@example.com>");
    REQUIRE(m.subject == "Hello world");
    REQUIRE(m.bodyPreview == "Hello world preview");
    REQUIRE(m.parentFolderId == "AAMkADExMzZkOWRiLTU0NjctNDI4ZS04MDU2LTk2N2E1NzMyZDU2");
}

TEST_CASE("M7 parseGraphMessage: body parsed",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.body.contentType == BodyType::Text);
    REQUIRE(m.body.content == "Hello world body");
}

TEST_CASE("M7 parseGraphMessage: dates preserved as strings",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.createdDateTime      == "2024-01-15T10:30:00Z");
    REQUIRE(m.lastModifiedDateTime == "2024-01-15T10:31:00Z");
    REQUIRE(m.sentDateTime         == "2024-01-15T10:30:05Z");
    REQUIRE(m.receivedDateTime     == "2024-01-15T10:30:10Z");
}

TEST_CASE("M7 parseGraphMessage: sender / from / replyTo",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.hasSender);
    REQUIRE(m.sender.name    == "Alice Sender");
    REQUIRE(m.sender.address == "alice@example.com");
    REQUIRE(m.hasFrom);
    REQUIRE(m.from.address == "alice@example.com");
    REQUIRE(m.hasReplyTo);
    REQUIRE(m.replyTo.address == "alice@example.com");
}

TEST_CASE("M7 parseGraphMessage: recipients To/Cc/Bcc populated independently",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.toRecipients.size()  == 2);
    REQUIRE(m.toRecipients[0].kind == RecipientKind::To);
    REQUIRE(m.toRecipients[0].emailAddress.name == "Bob");
    REQUIRE(m.toRecipients[1].emailAddress.address == "carol@example.com");

    REQUIRE(m.ccRecipients.size() == 1);
    REQUIRE(m.ccRecipients[0].kind == RecipientKind::Cc);
    REQUIRE(m.ccRecipients[0].emailAddress.address == "dave@example.com");

    REQUIRE(m.bccRecipients.empty());
}

TEST_CASE("M7 parseGraphMessage: flags + importance + flag.flagStatus",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.isRead);
    REQUIRE_FALSE(m.isDraft);
    REQUIRE(m.hasAttachments);
    REQUIRE(m.importance == Importance::High);
    REQUIRE(m.flagStatus == FlagStatus::Flagged);
}

TEST_CASE("M7 parseGraphMessage: categories array populated",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.categories.size() == 2);
    REQUIRE(m.categories[0] == "Work");
    REQUIRE(m.categories[1] == "Urgent");
}

TEST_CASE("M7 parseGraphMessage: internet headers preserved in order",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.internetMessageHeaders.size() == 2);
    REQUIRE(m.internetMessageHeaders[0].name == "Return-Path");
    REQUIRE(m.internetMessageHeaders[1].name == "X-Mailer");
}

TEST_CASE("M7 parseGraphMessage: file attachment + base64 decoded",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    REQUIRE(m.attachments.size() == 1);
    const auto& a = m.attachments[0];
    REQUIRE(a.kind == AttachmentKind::File);
    REQUIRE(a.name == "hello.txt");
    REQUIRE(a.contentType == "text/plain");
    REQUIRE_FALSE(a.isInline);
    REQUIRE(a.size == 5);
    REQUIRE(a.contentBytes.size() == 5);
    REQUIRE(a.contentBytes[0] == 'H');
    REQUIRE(a.contentBytes[4] == 'o');
}

TEST_CASE("M7 parseGraphMessage: conversationIndex base64 decoded",
          "[m7][phase_a][graph_message_parser]")
{
    const auto m = parseGraphMessage(kSampleMessageJson);
    // "AQHX1AAB" -> bytes
    REQUIRE(m.conversationIndex.size() == 6);
    REQUIRE(m.conversationIndex[0] == 0x01);
    REQUIRE(m.conversationIndex[1] == 0x01);
}

TEST_CASE("M7 parseGraphMessage: tolerates unknown fields",
          "[m7][phase_a][graph_message_parser]")
{
    const std::string json = R"({
        "subject": "x",
        "someNewFieldGraphAdded": "value",
        "anotherNumber": 42,
        "nestedUnknown": { "a": 1, "b": [true, false] }
    })";
    auto m = parseGraphMessage(json);  // must not throw
    REQUIRE(m.subject == "x");
}

TEST_CASE("M7 parseGraphMessage: HTML body type",
          "[m7][phase_a][graph_message_parser]")
{
    const std::string json = R"({
        "subject": "html",
        "body": { "contentType": "html", "content": "<p>Hi</p>" }
    })";
    auto m = parseGraphMessage(json);
    REQUIRE(m.body.contentType == BodyType::Html);
    REQUIRE(m.body.content == "<p>Hi</p>");
}

TEST_CASE("M7 parseGraphMessage: itemAttachment recursion",
          "[m7][phase_a][graph_message_parser]")
{
    const std::string json = R"({
        "subject": "outer",
        "attachments": [
            {
                "@odata.type": "#microsoft.graph.itemAttachment",
                "name": "embedded.eml",
                "size": 1024,
                "item": {
                    "@odata.type": "#microsoft.graph.message",
                    "subject": "inner",
                    "body": { "contentType": "text", "content": "inner body" }
                }
            }
        ]
    })";
    auto m = parseGraphMessage(json);
    REQUIRE(m.attachments.size() == 1);
    REQUIRE(m.attachments[0].kind == AttachmentKind::Item);
    REQUIRE(m.attachments[0].item != nullptr);
    REQUIRE(m.attachments[0].item->subject == "inner");
    REQUIRE(m.attachments[0].item->body.content == "inner body");
}

TEST_CASE("M7 parseGraphMessage: rejects malformed JSON",
          "[m7][phase_a][graph_message_parser]")
{
    REQUIRE_THROWS(parseGraphMessage("{ \"subject\":"));
    REQUIRE_THROWS(parseGraphMessage("not json"));
    REQUIRE_THROWS(parseGraphMessage("[]"));   // top-level must be object
}

TEST_CASE("M7 parseGraphMessage: utf-8 + escape sequences",
          "[m7][phase_a][graph_message_parser]")
{
    // Graph escapes Unicode via \u; embedded UTF-8 also possible.
    const std::string json = R"({
        "subject": "Café réunion",
        "body": { "contentType": "text", "content": "tab:\tnewline:\n" }
    })";
    auto m = parseGraphMessage(json);
    // U+00E9 is 0xC3 0xA9 in UTF-8.
    REQUIRE(m.subject.find("\xC3\xA9") != std::string::npos);
    REQUIRE(m.body.content == "tab:\tnewline:\n");
}

TEST_CASE("M7 parseGraphMessageList: array form",
          "[m7][phase_a][graph_message_parser]")
{
    const std::string json = R"([
        {"subject": "first"},
        {"subject": "second"}
    ])";
    auto v = parseGraphMessageList(json);
    REQUIRE(v.size() == 2);
    REQUIRE(v[0].subject == "first");
    REQUIRE(v[1].subject == "second");
}

TEST_CASE("M7 parseGraphMessageList: {value:[...]} envelope",
          "[m7][phase_a][graph_message_parser]")
{
    const std::string json = R"({
        "@odata.context": "...",
        "value": [
            {"subject": "x"},
            {"subject": "y"},
            {"subject": "z"}
        ]
    })";
    auto v = parseGraphMessageList(json);
    REQUIRE(v.size() == 3);
    REQUIRE(v[2].subject == "z");
}
