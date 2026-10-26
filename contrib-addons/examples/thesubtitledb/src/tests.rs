// SPDX-License-Identifier: GPL-2.0-or-later
use super::*;

fn invoke(id: &str, identity: Value) -> Value {
    json!({"type":"invoke","id":id,"contribution":"search","operation":"searchAssets",
        "context":{"identity":identity,"language":"en"},"credentials":{"apiKey":{"present":true}}})
}

fn movie() -> Value {
    json!({"kind":"movie","title":"Example Movie","year":2020,"ids":{"imdb":"tt1234567"}})
}

fn reply(call: &Value, status: u64, body: &str) -> Value {
    json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],"status":"ok",
        "response":{"status":status,"body":body,"headers":{}}})
}

const MOVIE: &str = include_str!("../tests/fixtures/movie.json");
const EPISODE: &str = include_str!("../tests/fixtures/episode.json");

#[test]
fn missing_key_is_setup_required_without_network() {
    let mut provider = Provider::default();
    let mut request = invoke("lookup", movie());
    request["credentials"]["apiKey"]["present"] = json!(false);
    let messages = provider.handle(&request);
    assert_eq!(messages.len(), 1);
    assert_eq!(messages[0]["status"], "auth_required");
    assert!(provider.pending.is_empty());
}

#[test]
fn movie_uses_brokered_scoped_lookup_and_secret_reference_only() {
    let mut provider = Provider::default();
    let messages = provider.handle(&invoke("lookup", movie()));
    assert_eq!(messages[0]["operation"], "network.http");
    assert_eq!(messages[0]["request"]["credential"], "apiKey");
    let url = messages[0]["request"]["url"].as_str().unwrap();
    assert!(url.starts_with("https://api.thesubtitledb.org/v1/by-imdb/tt1234567?"));
    assert!(!messages[0].to_string().contains("Authorization"));
}

#[test]
fn nested_episode_identity_never_uses_movie_or_episode_id_as_series() {
    let mut provider = Provider::default();
    let identity = json!({"kind":"episode","titles":["Example Show"],"episode":{"scheme":"seasonal","season":2,"episodes":[3]},
        "externalIds":[{"namespace":"imdb","scope":"series","value":"tt7654321"}]});
    let messages = provider.handle(&invoke("lookup", identity));
    assert!(messages[0]["request"]["url"]
        .as_str()
        .unwrap()
        .contains("/by-imdb/tt7654321/season/2/episode/3?"));
    let results = provider.handle(&reply(&messages[0], 200, EPISODE));
    assert_eq!(results[0]["data"]["candidates"][0]["identity"]["season"], 2);
    assert_eq!(
        results[0]["data"]["candidates"][0]["identity"]["episodes"],
        json!([3])
    );
}

#[test]
fn combined_and_absolute_episodes_are_not_guessed() {
    let mut provider = Provider::default();
    for identity in [
        json!({"kind":"episode","season":1,"episodes":[1,2]}),
        json!({"kind":"episode","episode":{"scheme":"absolute","season":1,"episodes":[2]}}),
    ] {
        assert_eq!(
            provider.handle(&invoke("lookup", identity))[0]["status"],
            "unsupported"
        );
    }
    assert!(provider.pending.is_empty());
}

#[test]
fn title_query_is_percent_encoded_and_episode_is_then_scoped() {
    let mut provider = Provider::default();
    let mut request = invoke(
        "lookup",
        json!({"kind":"episode","title":"A & B / 東京","season":2,"episodes":[3]}),
    );
    request["context"]["query"] = json!("A & B / 東京");
    let first = provider.handle(&request);
    let url = first[0]["request"]["url"].as_str().unwrap();
    assert!(url.contains("q=A%20%26%20B%20%2F%20%E6%9D%B1%E4%BA%AC&type=tv"));
    let second = provider.handle(&reply(&first[0], 200, EPISODE));
    assert!(second[0]["request"]["url"]
        .as_str()
        .unwrap()
        .contains("/by-imdb/tt7654321/season/2/episode/3?"));
    let results = provider.handle(&reply(&second[0], 200, EPISODE));
    assert_eq!(
        results[0]["data"]["candidates"][0]["evidence"]["source"],
        "title-lookup"
    );
}

#[test]
fn explicit_manual_query_overrides_stale_original_ids() {
    let mut provider = Provider::default();
    let mut request = invoke("lookup", movie());
    request["context"]["identity"]["ids"]["imdb"] = json!("tt9999999");
    request["context"]["identity"]["externalIds"] =
        json!([{"namespace":"imdb","scope":"movie","value":"tt9999999"}]);
    request["context"]["manual"] = json!(true);
    request["context"]["query"] = json!("Example Movie");
    let call = provider.handle(&request).remove(0);
    assert!(call["request"]["url"]
        .as_str()
        .unwrap()
        .contains("/by-title?q=Example%20Movie&type=movie"));
    let result = provider.handle(&reply(&call, 200, MOVIE)).remove(0);
    let candidate = &result["data"]["candidates"][0];
    assert_eq!(candidate["identity"]["ids"]["imdb"], "tt1234567");
    assert!(candidate["identity"].get("externalIds").is_none());
    assert_eq!(candidate["evidence"]["source"], "title-lookup");
}

#[test]
fn plain_manual_title_and_all_languages_do_not_guess_a_tv_episode() {
    let mut provider = Provider::default();
    let mut request = invoke("lookup", json!({"type":"unknown","title":"Example Movie"}));
    request["context"]["manual"] = json!(true);
    request["context"]["query"] = json!("Example Movie");
    request["context"]["language"] = json!("");
    let call = provider.handle(&request).remove(0);
    let url = call["request"]["url"].as_str().unwrap();
    assert!(url.contains("/by-title?q=Example%20Movie&limit="));
    assert!(!url.contains("lang="));
    assert!(!url.contains("type="));
    let result = provider.handle(&reply(&call, 200, MOVIE)).remove(0);
    assert_eq!(result["data"]["candidates"][0]["identity"]["kind"], "movie");
    let call = provider.handle(&request).remove(0);
    assert_eq!(
        provider.handle(&reply(&call, 200, EPISODE))[0]["status"],
        "unsupported"
    );
}

#[test]
fn response_projection_preserves_identity_language_and_uncertain_hash() {
    let mut provider = Provider::default();
    let calls = provider.handle(&invoke("lookup", movie()));
    let results = provider.handle(&reply(&calls[0], 200, MOVIE));
    let candidates = results[0]["data"]["candidates"].as_array().unwrap();
    assert_eq!(candidates.len(), 2);
    assert_eq!(candidates[0]["language"], "en");
    assert_eq!(candidates[1]["language"], "pt-BR");
    assert_eq!(candidates[0]["identity"]["ids"]["imdb"], "tt1234567");
    assert_eq!(candidates[0]["evidence"]["hashMatched"], false);
    assert_eq!(results[0]["data"]["cursor"], "2");
}

#[test]
fn forged_completion_cannot_steal_another_invocation() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("lookup", movie())).remove(0);
    let mut wrong = reply(&call, 200, MOVIE);
    wrong["invocation"] = json!("other");
    assert!(provider.handle(&wrong).is_empty());
    assert_eq!(provider.pending.len(), 1);
    assert_eq!(
        provider.handle(&reply(&call, 200, MOVIE))[0]["status"],
        "match"
    );
    assert!(provider.handle(&reply(&call, 200, MOVIE)).is_empty());
}

#[test]
fn canceled_invocations_ignore_late_completions() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("lookup", movie())).remove(0);
    provider.handle(&json!({"type":"cancel","id":"lookup"}));
    assert!(provider.handle(&reply(&call, 200, MOVIE)).is_empty());
    assert!(provider.issued.is_empty());
}

#[test]
fn unauthorized_rate_limited_and_transport_failures_are_not_no_match() {
    for (http, expected) in [
        (401, "auth_required"),
        (403, "auth_required"),
        (429, "rate_limited"),
        (503, "rate_limited"),
        (500, "error"),
        (404, "no_match"),
    ] {
        let mut provider = Provider::default();
        let call = provider.handle(&invoke("lookup", movie())).remove(0);
        let mut response = reply(&call, http, "{}");
        response["response"]["headers"]["retry-after"] = json!("120");
        let results = provider.handle(&response);
        assert_eq!(results[0]["status"], expected);
        if expected == "rate_limited" {
            assert_eq!(results[0]["data"]["retryAfterSeconds"], 120);
        }
    }
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("lookup", movie())).remove(0);
    let response = json!({"type":"host-result","id":call["id"],"invocation":"lookup","status":"error","error":"must not echo arbitrary URL/token"});
    let result = provider.handle(&response);
    assert_eq!(result[0]["status"], "error");
    assert!(!result[0].to_string().contains("token"));
}

#[test]
fn malformed_or_conflicting_api_response_is_rejected() {
    for response in [
        "<html>bad gateway</html>",
        "{}",
        r#"{"title":{"imdb":"tt9999999","media_type":"movie"},"subtitles":{"items":[]}}"#,
    ] {
        let mut provider = Provider::default();
        let call = provider.handle(&invoke("lookup", movie())).remove(0);
        assert_eq!(
            provider.handle(&reply(&call, 200, response))[0]["status"],
            "error"
        );
    }
}

#[test]
fn resolve_uses_only_an_issued_id_and_constructs_a_trusted_api_path() {
    let mut provider = Provider::default();
    let mut request = invoke("resolve", movie());
    request["operation"] = json!("resolveAsset");
    request["context"] = json!({"candidate":{"id":"999"}});
    assert_eq!(provider.handle(&request)[0]["status"], "error");
    let call = provider.handle(&invoke("lookup", movie())).remove(0);
    let results = provider.handle(&reply(&call, 200, MOVIE));
    let candidate = results[0]["data"]["candidates"][0].clone();
    request["context"] = json!({"candidate":candidate});
    let resolved = provider.handle(&request);
    assert_eq!(
        resolved[0]["data"]["url"],
        "https://api.thesubtitledb.org/get/10001"
    );
    assert_eq!(resolved[0]["data"]["fileName"], "thesubtitledb-10001.srt");
    assert!(!resolved[0].to_string().contains("malicious.example"));
    request["context"]["candidate"]["identity"]["title"] = json!("Different movie");
    assert_eq!(provider.handle(&request)[0]["status"], "error");
}

#[test]
fn bounded_language_cursor_and_concurrency() {
    let mut provider = Provider::default();
    let mut request = invoke("lookup", movie());
    request["context"]["language"] = json!("en&token=bad");
    assert_eq!(provider.handle(&request)[0]["status"], "error");
    request["context"]["language"] = json!("en");
    request["context"]["cursor"] = json!("10001");
    assert_eq!(provider.handle(&request)[0]["status"], "unsupported");
    for n in 0..MAX_PENDING {
        assert_eq!(
            provider.handle(&invoke(&format!("job{n}"), movie()))[0]["type"],
            "host-call"
        );
    }
    assert_eq!(
        provider.handle(&invoke("overflow", movie()))[0]["status"],
        "error"
    );
}
