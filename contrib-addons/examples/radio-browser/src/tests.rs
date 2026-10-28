use super::*;
fn request(id: &str, operation: &str, context: Value) -> Value {
    json!({"type":"invoke","id":id,"contribution":"stations","operation":operation,"context":context,
        "settings":{},"credentials":{},"hostCapabilities":{"mediaStreamAvailable":true}})
}
fn send(addon: &mut RadioBrowser, value: Value) -> Vec<Value> {
    addon.handle(Incoming::parse(&value).unwrap())
}
fn invoke(addon: &mut RadioBrowser, id: &str, operation: &str, context: Value) -> Value {
    let mut values = send(addon, request(id, operation, context));
    assert_eq!(values.len(), 1);
    values.remove(0)
}
fn station_value(n: usize) -> Value {
    json!({"stationuuid":format!("550e8400-e29b-41d4-a716-{n:012x}"),"name":"Station",
        "lastcheckok":1,"ssl_error":0,"codec":"MP3","hls":0,"url_resolved":"https://stream.example.org/live.mp3",
        "url":"https://original.example.org/list.pls","favicon":"https://images.example.org/cover.png","homepage":"https://example.org"})
}
fn complete(addon: &mut RadioBrowser, call: &Value, rows: Value) -> Vec<Value> {
    send(
        addon,
        json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],"status":"ok",
        "response":{"status":200,"body":rows.to_string(),"headers":{}}}),
    )
}
fn roots(addon: &mut RadioBrowser, id: &str) -> Value {
    invoke(addon, id, "browse", json!({"nodeRef":"popular"}))
}
#[test]
fn roots_are_static_and_do_not_request_network() {
    let value = invoke(&mut RadioBrowser::default(), "roots", "roots", json!({}));
    assert_eq!(value["type"], "result");
    assert_eq!(value["data"]["items"].as_array().unwrap().len(), 4);
    assert_eq!(value["data"]["items"][0]["id"], "popular");
}
#[test]
fn names_are_encoded_inside_fixed_api_origins() {
    let call = invoke(
        &mut RadioBrowser::default(),
        "search",
        "search",
        json!({"query":"音 &name=other/#", "filters":{}}),
    );
    assert_eq!(call["operation"], "network.http");
    assert!(call["request"]["url"]
        .as_str()
        .unwrap()
        .starts_with(MIRRORS[0]));
    assert!(call["request"]["url"]
        .as_str()
        .unwrap()
        .ends_with("&name=%E9%9F%B3%20%26name%3Dother%2F%23"));
    assert_eq!(call["request"]["responseBytes"], MAX_BODY);
}
#[test]
fn pages_have_stable_ids_and_no_addresses() {
    let mut addon = RadioBrowser::default();
    let call = roots(&mut addon, "page");
    let mut a = station_value(1);
    a["stationuuid"] = json!(a["stationuuid"].as_str().unwrap().to_ascii_uppercase());
    let mut b = a.clone();
    b["name"] = json!("A later display name");
    let reply = complete(&mut addon, &call, json!([a, b])).remove(0);
    let values = reply["data"]["items"].as_array().unwrap();
    assert_eq!(values.len(), 1);
    assert_eq!(values[0]["id"], station_value(1)["stationuuid"]);
    assert_eq!(values[0].as_object().unwrap().len(), 4);
    assert!(!reply.to_string().contains("https://"));
    assert!(!reply.to_string().contains("favicon"));
}
#[test]
fn public_url_syntax_denies_private_and_ambiguous_authorities() {
    for url in [
        "http://radio.example.org/a",
        "https://127.0.0.1/a",
        "https://[::1]/a",
        "https://2130706433/a",
        "https://radio.local/a",
        "https://radio.home.arpa/a",
        "https://localhost/a",
        "https://radio.internal/a",
        "https://example.org:444/a",
        "https://user@example.org/a",
        "https://example.org./a",
        "https://ex%61mple.org/a",
        "https://radio.invalid/a",
        "https://radio.test/a",
        "https://radio.example/a",
        "https://-radio.example.org/a",
        "https://example.org/a#fragment",
        "https://example.org/with space",
        "https://example.org/a\\b",
        "https://example.org/%ZZ",
        "https://example.org/音",
        "https://example.org.1/a",
    ] {
        assert!(!public_https(url), "{url}");
    }
    assert!(public_https("https://radio.example.org:443/live?x=%20"));
}
#[test]
fn unhealthy_or_unsupported_stations_are_removed() {
    let original = station_value(1);
    for (key, value) in [
        ("lastcheckok", json!(0)),
        ("ssl_error", json!(1)),
        ("codec", json!("H264")),
        ("hls", json!(2)),
        ("name", json!("")),
        ("name", json!("https://stream.example.org/live")),
        ("name", json!("Station\nother")),
        ("url_resolved", json!("https://stream.example.org/a.pls")),
        ("stationuuid", json!("00000000-0000-0000-0000-000000000000")),
    ] {
        let mut changed = original.clone();
        changed[key] = value;
        assert!(station(&changed).is_none(), "{key}");
    }
}
#[test]
fn hls_is_typed_and_an_outer_playlist_is_not_progressive_media() {
    let mut value = station_value(1);
    value["hls"] = json!(1);
    assert_eq!(station(&value).unwrap().format, "hls");
    value["hls"] = json!(0);
    value["url_resolved"] = json!("https://stream.example.org/a.m3u8?token=one");
    assert_eq!(station(&value).unwrap().format, "hls");
    for suffix in ["m3u", "pls", "asx", "xspf", "mpd"] {
        value["url_resolved"] = json!(format!("https://stream.example.org/a.{suffix}"));
        assert!(station(&value).is_none());
    }
}
#[test]
fn cursor_keeps_request_context_mirror_and_bounded_offset() {
    let mut addon = RadioBrowser::default();
    let call = roots(&mut addon, "page");
    let rows: Vec<_> = (0..PAGE).map(station_value).collect();
    let reply = complete(&mut addon, &call, json!(rows)).remove(0);
    let cursor = reply["data"]["cursor"].clone();
    let next = invoke(
        &mut addon,
        "next",
        "browse",
        json!({"nodeRef":"popular","cursor":cursor}),
    );
    assert!(next["request"]["url"]
        .as_str()
        .unwrap()
        .starts_with(MIRRORS[0]));
    assert!(next["request"]["url"]
        .as_str()
        .unwrap()
        .contains("offset=10"));
    for changed in [
        json!({"v":1,"key":"browse:jazz","offset":50,"mirror":0}),
        json!({"v":1,"key":"browse:popular","offset":51,"mirror":0}),
        json!({"v":1,"key":"browse:popular","offset":1000,"mirror":0}),
        json!({"v":1,"key":"browse:popular","offset":50,"mirror":2}),
        json!({"v":1,"key":"browse:popular","offset":50,"mirror":0,"extra":true}),
    ] {
        assert_eq!(
            invoke(
                &mut addon,
                "bad",
                "browse",
                json!({"nodeRef":"popular","cursor":changed})
            )["status"],
            "error"
        );
    }
}
#[test]
fn an_empty_filtered_page_can_continue_without_an_unbounded_scan() {
    let mut addon = RadioBrowser::default();
    let call = roots(&mut addon, "page");
    let reply = complete(&mut addon, &call, json!(vec![json!({}); PAGE])).remove(0);
    assert_eq!(reply["status"], "match");
    assert!(reply["data"]["items"].as_array().unwrap().is_empty());
    assert_eq!(reply["data"]["cursor"]["offset"], 10);
    let call = invoke(
        &mut addon,
        "end",
        "browse",
        json!({"nodeRef":"popular","cursor":{"v":1,"key":"browse:popular","offset":990,"mirror":0}}),
    );
    let reply = complete(&mut addon, &call, json!(vec![json!({}); PAGE])).remove(0);
    assert_eq!(reply["status"], "no_match");
    assert!(reply["data"].get("cursor").is_none());
}
#[test]
fn replay_fetches_the_uuid_again_and_never_reuses_a_stream() {
    let mut addon = RadioBrowser::default();
    let value = station_value(1);
    let id = value["stationuuid"].clone();
    let a = invoke(
        &mut addon,
        "first",
        "resolve",
        json!({"itemRef":id,"intent":"play"}),
    );
    assert!(a["request"]["url"]
        .as_str()
        .unwrap()
        .contains("/json/stations/byuuid?uuids=550e8400-"));
    let reply = complete(&mut addon, &a, json!([value.clone()])).remove(0);
    assert_eq!(reply["status"], "match");
    let b = invoke(
        &mut addon,
        "second",
        "resolve",
        json!({"itemRef":id,"intent":"play"}),
    );
    assert_eq!(b["type"], "host-call");
    assert_ne!(a["id"], b["id"]);
    let mut changed = value;
    changed["url_resolved"] = json!("https://other.example.org/new.mp3");
    assert_eq!(
        complete(&mut addon, &b, json!([changed])).remove(0)["data"]["url"],
        "https://other.example.org/new.mp3"
    );
}
#[test]
fn resolve_needs_a_native_play_intent_and_current_station_identity() {
    let mut addon = RadioBrowser::default();
    for context in [
        json!({"itemRef":"../escape","intent":"play"}),
        json!({"itemRef":station_value(1)["stationuuid"],"intent":"enqueue"}),
    ] {
        assert_eq!(
            invoke(&mut addon, "invalid", "resolve", context)["type"],
            "result"
        );
    }
    let mut value = request(
        "no-host",
        "resolve",
        json!({"itemRef":station_value(1)["stationuuid"],"intent":"play"}),
    );
    value["hostCapabilities"]["mediaStreamAvailable"] = json!(false);
    assert_eq!(send(&mut addon, value)[0]["status"], "unsupported");
    let call = invoke(
        &mut addon,
        "resolve",
        "resolve",
        json!({"itemRef":station_value(1)["stationuuid"],"intent":"play"}),
    );
    assert_eq!(
        complete(&mut addon, &call, json!([station_value(2)])).remove(0)["status"],
        "no_match"
    );
}
#[test]
fn foreign_or_late_host_responses_cannot_finish_an_invocation() {
    let mut addon = RadioBrowser::default();
    let call = roots(&mut addon, "page");
    assert!(send(&mut addon, json!({"type":"host-result","id":call["id"],"invocation":"foreign","status":"ok","response":{"status":200,"body":"[]"}})).is_empty());
    assert_eq!(addon.pending.len(), 1);
    assert!(send(&mut addon, json!({"type":"cancel","id":"page"})).is_empty());
    assert!(complete(&mut addon, &call, json!([station_value(1)])).is_empty());
    assert!(addon.active.is_empty());
}
#[test]
fn mirror_retry_is_bounded_and_keeps_the_exact_path() {
    let mut addon = RadioBrowser::default();
    let a = roots(&mut addon, "page");
    let b = send(&mut addon, json!({"type":"host-result","id":a["id"],"invocation":"page","status":"error","error":"Offline"})).remove(0);
    assert!(b["request"]["url"]
        .as_str()
        .unwrap()
        .starts_with(MIRRORS[0]));
    assert_eq!(
        a["request"]["url"]
            .as_str()
            .unwrap()
            .strip_prefix(MIRRORS[0]),
        b["request"]["url"]
            .as_str()
            .unwrap()
            .strip_prefix(MIRRORS[0])
    );
    let end = send(&mut addon, json!({"type":"host-result","id":b["id"],"invocation":"page","status":"ok","response":{"status":503,"body":"[]"}})).remove(0);
    assert_eq!(end["status"], "error");
    assert!(addon.pending.is_empty());
    assert!(addon.active.is_empty());
}
#[test]
fn malformed_or_oversize_pages_have_no_partial_result() {
    for body in [
        "{".to_owned(),
        "{}".to_owned(),
        "x".repeat(MAX_BODY + 1),
        json!(vec![station_value(1); PAGE + 1]).to_string(),
    ] {
        let mut addon = RadioBrowser::default();
        let call = roots(&mut addon, "page");
        let end = send(&mut addon, json!({"type":"host-result","id":call["id"],"invocation":"page","status":"ok","response":{"status":200,"body":body}})).remove(0);
        assert_eq!(end["status"], "error");
        assert!(end["data"].get("items").is_none());
    }
}
#[test]
fn names_and_filters_are_bounded_before_network_access() {
    let mut addon = RadioBrowser::default();
    for query in [
        "".to_owned(),
        "x".repeat(513),
        "🎵".repeat(257),
        "Name\nother".to_owned(),
    ] {
        assert_eq!(
            invoke(&mut addon, "bad", "search", json!({"query":query}))["type"],
            "result"
        );
    }
    assert_eq!(
        invoke(
            &mut addon,
            "filter",
            "search",
            json!({"query":"Station","filters":{"origin":"https://other.example.org"}})
        )["status"],
        "unsupported"
    );
}
#[test]
fn active_quota_duplicate_ids_and_counter_exhaustion_fail_closed() {
    let mut addon = RadioBrowser::default();
    for index in 0..MAX_ACTIVE {
        assert_eq!(
            roots(&mut addon, &format!("id-{index}"))["type"],
            "host-call"
        );
    }
    assert_eq!(roots(&mut addon, "overflow")["status"], "error");
    assert!(send(
        &mut addon,
        request("id-0", "browse", json!({"nodeRef":"popular"}))
    )
    .is_empty());
    send(&mut addon, json!({"type":"cancel","id":"id-0"}));
    assert_eq!(roots(&mut addon, "replacement")["type"], "host-call");
    let mut exhausted = RadioBrowser::default();
    exhausted.sequence = u64::MAX;
    assert_eq!(roots(&mut exhausted, "end")["status"], "error");
    assert!(exhausted.active.is_empty());
}

#[test]
fn an_incomplete_response_cannot_publish_a_valid_prefix() {
    let mut addon = RadioBrowser::default();
    let call = roots(&mut addon, "page");
    let value = send(
        &mut addon,
        json!({"type":"host-result","id":call["id"],"invocation":"page","status":"ok",
        "response":{"status":200,"body":json!([station_value(1)]).to_string(),"truncated":true}}),
    )
    .remove(0);
    assert_eq!(value["status"], "error");
    assert!(value["data"].get("items").is_none());
}
