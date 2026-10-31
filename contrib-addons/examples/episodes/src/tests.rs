// SPDX-License-Identifier: GPL-2.0-or-later
// The rules of the app's former built-in Episodes panel, case by case:
// grouping, labels, the open row, versions, marks and play labels.
use super::*;

fn file(id: &str, season: i64, episodes: &[i64], version: i64) -> Sibling {
    Sibling {
        id: id.into(),
        kind: "folder".into(),
        name: format!("{id}.mkv"),
        season,
        episodes: episodes.to_vec(),
        version,
        video: true,
        ..Sibling::default()
    }
}

fn listing(items: Vec<Sibling>) -> Listing {
    let mut seasons: Vec<(i64, u64)> = Vec::new();
    for item in items.iter().filter(|item| item.version == 0) {
        match seasons.iter_mut().find(|(season, _)| *season == item.season) {
            Some(entry) => entry.1 += 1,
            None => seasons.push((item.season, 1)),
        }
    }
    seasons.sort();
    Listing {
        kind: items.first().map(|item| item.kind.clone()).unwrap_or_default(),
        show: "Show".into(),
        total: items.len() as u64,
        current_season: items.iter().find(|item| item.current).map(|item| item.season),
        seasons,
        items,
        ..Listing::default()
    }
}

fn rows(state: &Value) -> Vec<Value> {
    state["episodes"].as_array().cloned().unwrap_or_default()
}

#[test]
fn versions_follow_their_first_file() {
    let items = [file("a", 1, &[1], 0), file("b", 1, &[1], 1), file("c", 1, &[2], 0), file("d", 1, &[2], 0)];
    let groups = group(&items);
    let ids: Vec<Vec<&str>> = groups.iter().map(|group| group.iter().map(|item| item.id.as_str()).collect()).collect();
    assert!(group(&[]).is_empty());
    assert_eq!(ids, vec![vec!["a", "b"], vec!["c"], vec!["d"]]);
}

#[test]
fn labels_match_the_built_in_panel() {
    assert_eq!(season_label(0), "Specials");
    assert_eq!(season_label(-1), "Episodes");
    assert_eq!(season_label(3), "Season 3");
    assert_eq!(number(&file("a", 1, &[1, 2], 0)), "1–2");
    let absolute = Sibling { absolute: "452".into(), ..Sibling::default() };
    assert_eq!(number(&absolute), "452");
    let daily = Sibling { air_date: "2024-03-15".into(), ..Sibling::default() };
    assert_eq!(number(&daily), "2024-03-15");
    assert_eq!(title(&file("a", 1, &[4], 0)), "Episode 4");
    assert_eq!(title(&Sibling::default()), "Episode");
    let named = Sibling { title: "Pilot".into(), ..file("a", 1, &[1], 0) };
    assert_eq!(title(&named), "Pilot");
}

#[test]
fn the_playing_episode_opens_with_its_version() {
    let mut second = file("b", 1, &[1], 1);
    second.current = true;
    let mut watched = file("c", 1, &[2], 0);
    watched.played = true;
    watched.progress = 0.4;
    let state = render(&listing(vec![file("a", 1, &[1], 0), second, watched]), &Choice::reset(1.0), 1);
    let rows = rows(&state);
    assert_eq!(rows.len(), 2);
    assert_eq!(rows[0]["expanded"], true);
    assert_eq!(rows[0]["mark"], "Now playing");
    assert_eq!(rows[0]["multiple"], true);
    assert_eq!(rows[0]["version"], "b");
    assert_eq!(rows[0]["playKey"], "b");
    assert_eq!(rows[0]["playLabel"], "Playing");
    assert_eq!(rows[0]["playEnabled"], false);
    assert_eq!(rows[0]["versions"][1]["label"], "b.mkv");
    assert_eq!(rows[1]["expanded"], Value::Null);
    assert_eq!(rows[1]["mark"], Value::Null);
    assert_eq!(rows[1]["subtitle"], "Watched");
    assert_eq!(rows[1]["playLabel"], "Resume");
    assert_eq!(rows[1]["progress"], 0.4);
    assert_eq!(state["seasonLabel"], "Season 1");
    assert_eq!(state["files"], true);
}

#[test]
fn a_chosen_row_and_version_stay_chosen() {
    let mut playing = file("c", 1, &[2], 0);
    playing.current = true;
    let items = vec![file("a", 1, &[1], 0), file("b", 1, &[1], 1), playing];
    let choice = Choice { revision: 1.0, expanded: "a".into(), version_episode: "a".into(), version: "b".into(), ..Choice::default() };
    let rows = rows(&render(&listing(items.clone()), &choice, 1));
    assert_eq!(rows[0]["expanded"], true);
    assert_eq!(rows[0]["playKey"], "b");
    assert_eq!(rows[0]["playLabel"], "Play");
    assert_eq!(rows[1]["expanded"], Value::Null);
    let closed = Choice { expanded: "collapsed".into(), ..Choice::reset(1.0) };
    assert!(rows_closed(&render(&listing(items), &closed, 1)));
}

fn rows_closed(state: &Value) -> bool {
    rows(state).iter().all(|row| row["expanded"] != true)
}

#[test]
fn a_podcast_offers_its_video_and_says_listened() {
    let feed = |id: &str, version: i64| Sibling { kind: "feed".into(), name: String::new(), video: version > 0, ..file(id, -1, &[], version) };
    let mut audio = feed("a", 0);
    audio.played = true;
    audio.title = "First show".into();
    let items = vec![audio, feed("a-video", 1), feed("b", 0)];
    let open = Choice { expanded: "a".into(), ..Choice::reset(1.0) };
    let state = render(&listing(items), &open, -1);
    let rows = rows(&state);
    assert_eq!(rows.len(), 2);
    assert_eq!(rows[0]["subtitle"], "Listened");
    assert_eq!(rows[0]["multiple"], false);
    assert_eq!(rows[0]["hasVideo"], true);
    assert_eq!(rows[0]["videoKey"], "a-video");
    assert_eq!(rows[0]["hasFile"], false);
    assert_eq!(rows[1]["title"], "Episode");
    // A closed row carries only what the row shows.
    assert_eq!(rows[1]["hasVideo"], Value::Null);
    assert_eq!(rows[1]["playLabel"], "Play");
    assert_eq!(state["files"], false);
    assert_eq!(state["seasonLabel"], "Episodes");
}

#[test]
fn the_shown_season_prefers_the_choice_then_the_playing_one() {
    let mut playing = file("b", 2, &[1], 0);
    playing.current = true;
    let listing = listing(vec![file("a", 1, &[1], 0), playing, file("c", 3, &[1], 0)]);
    assert_eq!(season(&listing, &Choice::reset(1.0)), Some(2));
    assert_eq!(season(&listing, &Choice { season: Some(3), ..Choice::reset(1.0) }), Some(3));
    assert_eq!(season(&listing, &Choice { season: Some(9), ..Choice::reset(1.0) }), Some(2));
    let without = Listing { current_season: None, ..listing };
    assert_eq!(season(&without, &Choice::reset(1.0)), Some(1));
}

#[test]
fn notices_and_paging() {
    let mut list = listing((0..PAGE as i64).map(|n| file(&format!("e{n}"), 1, &[n + 1], 0)).collect());
    list.total = 75;
    list.limited = true;
    let state = render(&list, &Choice::reset(1.0), 1);
    assert_eq!((state["more"].clone(), state["less"].clone(), state["page"].clone()), (json!(true), json!(false), json!(0)));
    assert_eq!(state["hasNotice"], true);
    assert!(state["notice"].as_str().unwrap().starts_with("Some folders"));
    list.offset = PAGE;
    let state = render(&list, &Choice::reset(1.0), 1);
    assert_eq!((state["more"].clone(), state["less"].clone(), state["page"].clone()), (json!(false), json!(true), json!(1)));
    list.message = "Choose the series folder to look for other episodes.".into();
    assert_eq!(render(&list, &Choice::reset(1.0), 1)["notice"], "Choose the series folder to look for other episodes.");
    // A closed row leaves empty values out.
    let row = &rows(&state)[0];
    assert_eq!(row.as_object().unwrap().len(), 5, "{row}");
}

// The message flow, with a host that answers media.siblings.
fn invoke(addon: &mut Episodes, operation: &str, context: Value) -> Vec<Value> {
    addon.invoke("call", operation, context.as_object().unwrap())
}

fn answer(addon: &mut Episodes, call: &Value, response: Value) -> Vec<Value> {
    addon.complete(call["id"].as_str().unwrap(), call["invocation"].as_str().unwrap(), true, response.as_object())
}

fn reply(listing: &Listing) -> Value {
    json!({
        "kind": listing.kind, "show": listing.show, "loading": false, "limited": false, "total": listing.total,
        "currentSeason": listing.current_season,
        "seasons": listing.seasons.iter().map(|(season, count)| json!({"season": season, "count": count})).collect::<Vec<_>>(),
        "items": listing.items.iter().map(|item| json!({
            "id": item.id, "kind": item.kind, "name": item.name, "title": item.title, "season": item.season,
            "episodes": item.episodes, "version": item.version, "current": item.current, "played": item.played,
            "progress": item.progress, "video": item.video, "artwork": item.artwork})).collect::<Vec<_>>()
    })
}

#[test]
fn the_app_asks_whether_there_is_anything_to_show() {
    let mut addon = Episodes::default();
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 4, "kind": "video"}}));
    assert_eq!(calls[0]["type"], "host-call");
    assert_eq!(calls[0]["operation"], "media.siblings");
    assert_eq!(calls[0]["request"], json!({"limit": 1}));
    let done = answer(&mut addon, &calls[0], reply(&sample()));
    assert_eq!(done[0]["status"], "match");
    assert_eq!(done[0]["data"], json!({"available": true}));
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 5}}));
    let done = answer(&mut addon, &calls[0], json!({"kind": "none", "seasons": [], "items": []}));
    assert_eq!(done[0]["data"], json!({"available": false}));
}

#[test]
fn an_open_panel_reads_the_playing_season_and_its_artwork() {
    let mut addon = Episodes::default();
    let state = json!({"status": "loading", "revision": -1, "season": -1, "expanded": ""});
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    assert_eq!(calls[0]["request"], json!({"limit": 1}));
    let sample = sample();
    let calls = answer(&mut addon, &calls[0], reply(&sample));
    assert_eq!(calls[0]["request"]["season"], 1);
    assert_eq!(calls[0]["request"]["limit"], PAGE);
    let mut data = sample.clone();
    data.items.retain(|item| item.season == 1);
    let calls = answer(&mut addon, &calls[0], reply(&data));
    // The playing episode is open: read its details before the answer.
    assert_eq!(calls[0]["type"], "host-call");
    assert_eq!(calls[0]["request"]["details"], json!(["a"]));
    let done = answer(&mut addon, &calls[0], json!({"details": [{"id": "a", "description": "The first one.", "artwork": "a"}]}));
    let state = &done[0]["data"]["state"];
    assert_eq!(done[0]["data"]["available"], true);
    assert_eq!(state["status"], "ready");
    assert_eq!(state["revision"], 7.0);
    assert_eq!(state["season"], 1);
    assert_eq!(rows(state)[0]["playKey"], "b");
    assert_eq!(rows(state)[0]["description"], "The first one.");
    assert_eq!(rows(state)[0]["artwork"], "a");
}

#[test]
fn choices_survive_a_refresh_but_not_new_media() {
    let mut addon = Episodes::default();
    let state = json!({"status": "ready", "revision": 7, "season": 0, "expanded": "collapsed"});
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    assert_eq!(calls[0]["request"]["season"], 0);
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 8}, "state": state}));
    assert_eq!(calls[0]["request"], json!({"limit": 1}));
}

#[test]
fn rows_open_close_and_pick_a_version() {
    let mut addon = Episodes::default();
    let state = json!({"status": "ready", "revision": 7, "season": 1, "expanded": ""});
    let calls = invoke(&mut addon, "expand", json!({"arguments": {"key": "c", "open": false}, "state": state}));
    assert_eq!(calls[0]["request"]["details"], json!(["c"]));
    // The host answers the details it was asked for with the page.
    let mut page = reply(&sample());
    page["details"] = json!([{"id": "c", "description": "Second."}]);
    let done = answer(&mut addon, &calls[0], page);
    assert_eq!(done[0]["data"]["state"]["expanded"], "c");
    assert_eq!(rows(&done[0]["data"]["state"])[1]["description"], "Second.");
    let calls = invoke(&mut addon, "expand", json!({"arguments": {"key": "a", "open": true}, "state": state}));
    let done = answer(&mut addon, &calls[0], reply(&sample()));
    assert!(rows_closed(&done[0]["data"]["state"]));
    let calls = invoke(&mut addon, "expand", json!({"arguments": {"key": "a", "version": "a"}, "state": state}));
    let mut page = reply(&sample());
    page["details"] = json!([{"id": "a"}]);
    let done = answer(&mut addon, &calls[0], page);
    assert_eq!(rows(&done[0]["data"]["state"])[0]["playKey"], "a");
    assert_eq!(invoke(&mut addon, "expand", json!({"arguments": {}}))[0]["status"], "error");
}

#[test]
fn seasons_and_pages_read_again() {
    let mut addon = Episodes::default();
    let state = json!({"status": "ready", "revision": 7, "season": 1, "page": 1, "expanded": "c"});
    let calls = invoke(&mut addon, "season", json!({"arguments": {"season": 0}, "state": state}));
    assert_eq!(calls[0]["request"]["season"], 0);
    assert_eq!(calls[0]["request"]["offset"], 0);
    assert_eq!(calls[0]["request"]["details"], json!([]));
    let calls = invoke(&mut addon, "more", json!({"state": state}));
    assert_eq!((calls[0]["request"]["offset"].clone(), calls[0]["request"]["limit"].clone()), (json!(2 * PAGE), json!(PAGE)));
    let calls = invoke(&mut addon, "less", json!({"state": state}));
    assert_eq!(calls[0]["request"]["offset"], 0);
}

#[test]
fn the_playing_episode_page_opens_first() {
    let mut addon = Episodes::default();
    let state = json!({"status": "ready", "revision": 7, "season": 1, "expanded": "collapsed"});
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    assert_eq!(calls[0]["request"]["offset"], 0);
    let mut first = reply(&sample());
    first["currentOffset"] = json!(PAGE + 5);
    first["total"] = json!(2 * PAGE);
    let calls = answer(&mut addon, &calls[0], first);
    assert_eq!(calls[0]["request"]["offset"], PAGE);
    let mut second = reply(&sample());
    second["offset"] = json!(PAGE);
    second["total"] = json!(2 * PAGE);
    let done = answer(&mut addon, &calls[0], second);
    assert_eq!(done[0]["data"]["state"]["page"], 1);
    assert_eq!(done[0]["data"]["state"]["less"], true);
}

#[test]
fn clicks_become_app_effects() {
    let mut addon = Episodes::default();
    let done = invoke(&mut addon, "play", json!({"arguments": {"key": "media:1", "fromStart": true}}));
    assert_eq!(done[0]["data"], json!({"effect": {"play": {"id": "media:1", "fromStart": true}}}));
    let done = invoke(&mut addon, "refresh", json!({"arguments": {"folder": true}}));
    assert_eq!(done[0]["data"], json!({"effect": {"rescan": {"chooseFolder": true}}}));
    assert_eq!(invoke(&mut addon, "refresh", json!({}))[0]["data"], json!({"effect": {"rescan": {"chooseFolder": false}}}));
    assert_eq!(invoke(&mut addon, "identify", json!({}))[0]["data"], json!({"effect": {"identify": {}}}));
    assert_eq!(invoke(&mut addon, "source", json!({"arguments": {"key": "media:2"}}))[0]["data"], json!({"effect": {"source": {"id": "media:2"}}}));
    assert_eq!(invoke(&mut addon, "play", json!({"arguments": {}}))[0]["status"], "unsupported");
    assert_eq!(invoke(&mut addon, "delete", json!({}))[0]["status"], "unsupported");
}

#[test]
fn a_failed_read_is_an_error_and_media_without_a_series_is_empty() {
    let mut addon = Episodes::default();
    let state = json!({"status": "ready", "revision": 7, "season": 1});
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    let done = addon.complete(calls[0]["id"].as_str().unwrap(), "call", false, None);
    assert_eq!(done[0]["status"], "error");
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    let done = answer(&mut addon, &calls[0], json!({"kind": "none", "seasons": [], "items": []}));
    assert_eq!(done[0]["data"]["available"], false);
    assert_eq!(done[0]["data"]["state"]["status"], "none");
    // A reply for another invocation is ignored.
    let calls = invoke(&mut addon, "context", json!({"media": {"revision": 7}, "state": state}));
    assert!(addon.complete(calls[0]["id"].as_str().unwrap(), "other", true, None).is_empty());
}

#[test]
fn the_self_test_passes() {
    assert!(Episodes::default().self_test());
}

/// A season with a watched episode, the playing one in two versions, a
/// description and a title source. tests/sample-state.json is its state;
/// the app's corpus test checks that file against the app's own rules.
fn preview() -> Listing {
    let titles = ["", "", "", "", "", "", "", "", "", "Glass", "Old Harbour", "", "Ashes", "Return"];
    let item = |id: String, episode: i64, version: i64| Sibling {
        id,
        kind: "folder".into(),
        name: format!("The.Wand.S01E{episode:02}{}.mkv", if version > 0 { ".1080p" } else { "" }),
        title: titles[episode as usize - 1].into(),
        season: 1,
        episodes: vec![episode],
        version,
        video: true,
        ..Sibling::default()
    };
    let mut items = Vec::new();
    for episode in 9..=14 {
        items.push(item(format!("media:e{episode}"), episode, 0));
        if episode == 11 {
            items.push(item("media:e11b".into(), 11, 1));
        }
    }
    items[0].played = true;
    items[1].played = true;
    items[1].progress = 1.0;
    items[2].current = true;
    items[2].progress = 0.35;
    items[2].has_source = true;
    items[2].artwork = "media:e11".into();
    items[2].description = "Ronin follows a lead to the old harbour, where the wand's last owner waits.".into();
    Listing {
        kind: "folder".into(),
        show: "The Wand".into(),
        total: 7,
        seasons: vec![(0, 2), (1, 14), (2, 3)],
        current_season: Some(1),
        items,
        ..Listing::default()
    }
}

#[test]
fn the_sample_state_fixture_is_current() {
    use hakone_addon_message_v1::serde_json;
    let state = render(&preview(), &Choice::reset(1.0), 1);
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/sample-state.json");
    if std::env::var_os("EPISODES_WRITE_FIXTURE").is_some() {
        std::fs::write(path, serde_json::to_string_pretty(&state).unwrap() + "\n").unwrap();
    }
    let saved: Value = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    assert_eq!(saved, state, "Run the tests with EPISODES_WRITE_FIXTURE=1 to write tests/sample-state.json again.");
}
