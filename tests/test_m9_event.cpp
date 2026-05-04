// pstwriter/tests/test_m9_event.cpp
//
// M9 Phases A, B, C — Graph event JSON parser, buildEventPc,
// writeM9Pst end-to-end.

#include "event.hpp"
#include "graph_convert.hpp"
#include "graph_event.hpp"
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

graph::GraphEvent makeBasicEvent()
{
    graph::GraphEvent e;
    e.id                   = "event-1";
    e.iCalUId              = "040000008200E00074C5B7101A82E00800000000";
    e.subject              = "Project Sync";
    e.body.contentType     = graph::BodyType::Text;
    e.body.content         = "Weekly sync agenda...";
    e.bodyPreview          = "Weekly sync agenda";
    e.start.dateTime       = "2024-06-01T15:00:00";
    e.start.timeZone       = "UTC";
    e.end.dateTime         = "2024-06-01T16:00:00";
    e.end.timeZone         = "UTC";
    e.location.displayName = "Conference Room A";
    e.createdDateTime      = "2024-05-30T09:00:00Z";
    e.lastModifiedDateTime = "2024-05-30T09:01:00Z";
    e.importance           = graph::Importance::Normal;
    e.showAs               = graph::ShowAs::Busy;

    e.organizer.name    = "Alice Manager";
    e.organizer.address = "alice@example.com";
    e.hasOrganizer      = true;

    graph::Attendee bob;
    bob.kind     = graph::AttendeeKind::Required;
    bob.response = graph::AttendeeResponse::Accepted;
    bob.emailAddress.name    = "Bob";
    bob.emailAddress.address = "bob@example.com";
    e.attendees.push_back(bob);

    return e;
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
TEST_CASE("M9 parseGraphEvent: basic scalar fields",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "id": "event-1",
        "iCalUId": "040000008200E00074C5B7101A82E00800000000",
        "subject": "Project Sync",
        "createdDateTime": "2024-05-30T09:00:00Z"
    })";
    const auto e = graph::parseGraphEvent(json);
    REQUIRE(e.id == "event-1");
    REQUIRE(e.iCalUId == "040000008200E00074C5B7101A82E00800000000");
    REQUIRE(e.subject == "Project Sync");
    REQUIRE(e.createdDateTime == "2024-05-30T09:00:00Z");
}

TEST_CASE("M9 parseGraphEvent: dateTimeTimeZone fields",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "subject": "x",
        "start": { "dateTime": "2024-06-01T15:00:00", "timeZone": "UTC" },
        "end":   { "dateTime": "2024-06-01T16:00:00", "timeZone": "Pacific Standard Time" }
    })";
    const auto e = graph::parseGraphEvent(json);
    REQUIRE(e.start.dateTime == "2024-06-01T15:00:00");
    REQUIRE(e.start.timeZone == "UTC");
    REQUIRE(e.end.dateTime   == "2024-06-01T16:00:00");
    REQUIRE(e.end.timeZone   == "Pacific Standard Time");
}

TEST_CASE("M9 parseGraphEvent: location",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "subject": "x",
        "location": { "displayName": "Conference Room A", "locationType": "conferenceRoom" }
    })";
    const auto e = graph::parseGraphEvent(json);
    REQUIRE(e.location.displayName == "Conference Room A");
    REQUIRE(e.location.locationType == "conferenceRoom");
}

TEST_CASE("M9 parseGraphEvent: organizer + attendees",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "subject": "x",
        "organizer": { "emailAddress": { "name": "Alice", "address": "alice@ex.com" } },
        "attendees": [
            { "type": "required", "status": {"response": "accepted"},
              "emailAddress": {"name": "Bob", "address": "bob@ex.com"} },
            { "type": "optional", "status": {"response": "tentativelyAccepted"},
              "emailAddress": {"name": "Carol", "address": "carol@ex.com"} },
            { "type": "resource", "status": {"response": "none"},
              "emailAddress": {"name": "Room A", "address": "rooma@ex.com"} }
        ]
    })";
    const auto e = graph::parseGraphEvent(json);
    REQUIRE(e.hasOrganizer);
    REQUIRE(e.organizer.address == "alice@ex.com");
    REQUIRE(e.attendees.size() == 3);
    REQUIRE(e.attendees[0].kind == graph::AttendeeKind::Required);
    REQUIRE(e.attendees[0].response == graph::AttendeeResponse::Accepted);
    REQUIRE(e.attendees[1].kind == graph::AttendeeKind::Optional);
    REQUIRE(e.attendees[1].response == graph::AttendeeResponse::TentativelyAccepted);
    REQUIRE(e.attendees[2].kind == graph::AttendeeKind::Resource);
}

TEST_CASE("M9 parseGraphEvent: flags + showAs + type",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "subject": "x",
        "isAllDay": true,
        "isCancelled": false,
        "isOnlineMeeting": true,
        "isReminderOn": true,
        "reminderMinutesBeforeStart": 15,
        "showAs": "tentative",
        "type": "seriesMaster",
        "importance": "high"
    })";
    const auto e = graph::parseGraphEvent(json);
    REQUIRE(e.isAllDay);
    REQUIRE_FALSE(e.isCancelled);
    REQUIRE(e.isOnlineMeeting);
    REQUIRE(e.isReminderOn);
    REQUIRE(e.reminderMinutesBeforeStart == 15);
    REQUIRE(e.showAs == graph::ShowAs::Tentative);
    REQUIRE(e.type == graph::EventType::SeriesMaster);
    REQUIRE(e.importance == graph::Importance::High);
}

TEST_CASE("M9 parseGraphEvent: tolerates unknown fields",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "subject": "x",
        "futureField": {"nested": [1,2,3]},
        "anotherUnknown": true
    })";
    auto e = graph::parseGraphEvent(json);
    REQUIRE(e.subject == "x");
}

TEST_CASE("M9 parseGraphEventList: array form",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"([
        {"subject": "Event 1"},
        {"subject": "Event 2"}
    ])";
    auto v = graph::parseGraphEventList(json);
    REQUIRE(v.size() == 2);
    REQUIRE(v[0].subject == "Event 1");
}

TEST_CASE("M9 parseGraphEventList: {value:[...]} envelope",
          "[m9][phase_a][graph_event_parser]")
{
    const string json = R"({
        "@odata.context": "...",
        "value": [
            {"subject": "A"}, {"subject": "B"}, {"subject": "C"}
        ]
    })";
    auto v = graph::parseGraphEventList(json);
    REQUIRE(v.size() == 3);
}

// ============================================================================
// Phase B — buildEventPc
// ============================================================================
TEST_CASE("M9 buildEventPc: emits MessageClass IPM.Appointment",
          "[m9][phase_b][event_pc_round_trip]")
{
    const auto e  = makeBasicEvent();
    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* mc = findProp(props, 0x001Au);
    REQUIRE(mc != nullptr);
    REQUIRE(mc->propType == PropType::Unicode);
    // "IPM.Appointment" = 15 chars * 2 = 30 bytes
    REQUIRE(mc->valueSize == 30);
}

TEST_CASE("M9 buildEventPc: subject + body decoded back",
          "[m9][phase_b][event_pc_round_trip]")
{
    const auto e  = makeBasicEvent();
    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* subj = findProp(props, 0x0037u);
    REQUIRE(subj != nullptr);
    REQUIRE(subj->propType == PropType::Unicode);

    const auto* body = findProp(props, 0x1000u);
    REQUIRE(body != nullptr);
}

TEST_CASE("M9 buildEventPc: PidTagStartDate + EndDate populated",
          "[m9][phase_b][event_pc_round_trip]")
{
    const auto e  = makeBasicEvent();
    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* sd = findProp(props, 0x0060u);
    REQUIRE(sd != nullptr);
    REQUIRE(sd->propType == PropType::SystemTime);
    REQUIRE(sd->valueSize == 8);

    const auto* ed = findProp(props, 0x0061u);
    REQUIRE(ed != nullptr);
    REQUIRE(ed->propType == PropType::SystemTime);
    REQUIRE(ed->valueSize == 8);
}

TEST_CASE("M9 buildEventPc: HTML body emits PidTagBodyHtml",
          "[m9][phase_b][event_pc_round_trip]")
{
    auto e = makeBasicEvent();
    e.body.contentType = graph::BodyType::Html;
    e.body.content     = "<html><body>Agenda</body></html>";

    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* html = findProp(props, 0x1013u);
    REQUIRE(html != nullptr);
    REQUIRE(html->propType == PropType::Binary);
    REQUIRE(html->valueSize == e.body.content.size());
}

TEST_CASE("M9 buildEventPc: organizer maps to sender properties",
          "[m9][phase_b][event_pc_round_trip]")
{
    const auto e  = makeBasicEvent();
    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    REQUIRE(findProp(props, 0x0C1Au) != nullptr); // SenderName
    REQUIRE(findProp(props, 0x0C1Fu) != nullptr); // SenderEmailAddress
    REQUIRE(findProp(props, 0x0C19u) != nullptr); // SenderEntryId
    REQUIRE(findProp(props, 0x0042u) != nullptr); // SentRepresentingName
}

TEST_CASE("M9 buildEventPc: importance + sensitivity present",
          "[m9][phase_b][event_pc_round_trip]")
{
    auto e = makeBasicEvent();
    e.importance = graph::Importance::High;
    const auto pc = buildEventPc(e, makeCtx());
    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());

    const auto* imp = findProp(props, 0x0017u);
    REQUIRE(imp != nullptr);
    REQUIRE(imp->propType == PropType::Int32);
    REQUIRE(imp->inlineValue == 2u);  // Importance::High = 2

    REQUIRE(findProp(props, 0x0036u) != nullptr); // Sensitivity
}

TEST_CASE("M9 buildEventPc: minimal event (subject only) builds",
          "[m9][phase_b][event_pc_round_trip]")
{
    graph::GraphEvent e;
    e.subject = "Quick meeting";
    const auto pc = buildEventPc(e, makeCtx());
    REQUIRE_FALSE(pc.hnBytes.empty());

    const auto props = readPropertyContext(pc.hnBytes.data(), pc.hnBytes.size());
    REQUIRE(findProp(props, 0x001Au) != nullptr);
    REQUIRE(findProp(props, 0x0037u) != nullptr);
}

// ============================================================================
// Phase C — writeM9Pst end-to-end
// ============================================================================
TEST_CASE("M9 writeM9Pst: minimal flow produces a file",
          "[m9][phase_c][end_to_end]")
{
    M9PstConfig cfg;
    cfg.path = "m9_minimal.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M9 Minimal";

    M9CalendarFolder folder;
    folder.displayName = "Calendar";
    folder.parentNid   = Nid{0x00008022u};

    auto event = makeBasicEvent();
    folder.events.push_back(&event);

    cfg.folders = { folder };

    const auto r = writeM9Pst(cfg);
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

TEST_CASE("M9 writeM9Pst: full PST with multiple events in 2 folders",
          "[m9][phase_c][end_to_end][m9_pst_info]")
{
    M9PstConfig cfg;
    cfg.path = "m9_calendar.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M9 Calendar PST";

    auto sync = makeBasicEvent();

    graph::GraphEvent oneOnOne;
    oneOnOne.id              = "event-2";
    oneOnOne.subject         = "1:1 with Bob";
    oneOnOne.body.contentType = graph::BodyType::Html;
    oneOnOne.body.content     = "<p>Topics:<ul><li>Status</li></ul></p>";
    oneOnOne.start.dateTime  = "2024-06-02T10:00:00";
    oneOnOne.start.timeZone  = "UTC";
    oneOnOne.end.dateTime    = "2024-06-02T10:30:00";
    oneOnOne.end.timeZone    = "UTC";
    oneOnOne.organizer.name    = "Alice Manager";
    oneOnOne.organizer.address = "alice@example.com";
    oneOnOne.hasOrganizer      = true;
    oneOnOne.importance        = graph::Importance::High;

    graph::GraphEvent allDay;
    allDay.id        = "event-3";
    allDay.subject   = "Company Holiday";
    allDay.isAllDay  = true;
    allDay.start.dateTime = "2024-07-04T00:00:00";
    allDay.start.timeZone = "UTC";
    allDay.end.dateTime   = "2024-07-05T00:00:00";
    allDay.end.timeZone   = "UTC";
    allDay.showAs    = graph::ShowAs::Oof;

    M9CalendarFolder primary;
    primary.displayName = "Calendar";
    primary.parentNid   = Nid{0x00008022u};
    primary.events.push_back(&sync);
    primary.events.push_back(&oneOnOne);

    M9CalendarFolder personal;
    personal.displayName = "Personal Calendar";
    personal.parentNid   = Nid{0x00008022u};
    personal.events.push_back(&allDay);

    cfg.folders = { primary, personal };

    const auto r = writeM9Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);
}

TEST_CASE("M9 writeM9Pst: empty folder list still produces baseline PST",
          "[m9][phase_c][end_to_end]")
{
    M9PstConfig cfg;
    cfg.path = "m9_empty.pst";
    cfg.providerUid = {{
        0x22, 0x9D, 0xB5, 0x0A, 0xDC, 0xD9, 0x94, 0x43,
        0x85, 0xDE, 0x90, 0xAE, 0xB0, 0x7D, 0x12, 0x70,
    }};
    cfg.pstDisplayName = "M9 Empty";

    const auto r = writeM9Pst(cfg);
    INFO(r.message);
    REQUIRE(r.ok);

    const int rc = runPstInfo(cfg.path);
    REQUIRE(rc == 0);

    std::remove(cfg.path.c_str());
}
