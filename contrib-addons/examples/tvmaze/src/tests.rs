use super::*;
fn invoke(id: &str) -> Value {
    json!({"type":"invoke","id":id,"contribution":"episodes","operation":"enrich","context":{
        "identity":{"kind":"episode","title":"Example Series","year":2020,"season":1,"episodes":[2]},"fields":["episodeTitle"]}})
}
fn show() -> Value {
    json!({"id":111,"name":"Example Series","premiered":"2020-01-01","externals":{"imdb":"tt1234567","thetvdb":222}})
}
fn episode() -> Value {
    json!({"id":333,"name":"Example episode","season":1,"number":2,"url":"https://www.tvmaze.com/episodes/333/example"})
}
fn complete(provider: &mut Provider, call: &Value, body: Value) -> Vec<Value> {
    provider.handle(
        &json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],"status":"ok",
        "response":{"status":200,"body":body.to_string(),"headers":{}}}),
    )
}
fn start_episodes(provider: &mut Provider, request: &Value) -> Value {
    let search = provider.handle(request).remove(0);
    let seasons = complete(provider, &search, json!([{"show":show()}])).remove(0);
    assert_eq!(
        seasons["request"]["url"],
        "https://api.tvmaze.com/shows/111/seasons"
    );
    complete(provider, &seasons, json!([{"id":444,"number":1}])).remove(0)
}
#[test]
fn title_lookup_preserves_the_season_workflow() {
    let mut provider = Provider::default();
    let episodes = start_episodes(&mut provider, &invoke("a"));
    assert_eq!(
        episodes["request"]["url"],
        "https://api.tvmaze.com/seasons/444/episodes"
    );
    let result = complete(&mut provider, &episodes, json!([episode()])).remove(0);
    assert_eq!(result["status"], "match");
    assert_eq!(result["data"]["fields"]["episodeTitle"], "Example episode");
    assert_eq!(result["data"]["identity"]["ids"]["seriesImdb"], "tt1234567");
    assert_eq!(result["data"]["attribution"]["license"], "CC-BY-SA-4.0");
    assert!(provider.pending.is_empty());
}
#[test]
fn same_name_and_year_are_ambiguous() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("a")).remove(0);
    let result = complete(
        &mut provider,
        &call,
        json!([{"show":show()},{"show":show()}]),
    )
    .remove(0);
    assert_eq!(result["status"], "ambiguous");
    assert!(result["data"].get("fields").is_none());
}
#[test]
fn different_year_is_not_the_target() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("a")).remove(0);
    let mut other = show();
    other["premiered"] = json!("2010-01-01");
    assert_eq!(
        complete(&mut provider, &call, json!([{"show":other}]))[0]["status"],
        "no_match"
    );
}
#[test]
fn scoped_ids_are_verified_against_the_response() {
    let mut provider = Provider::default();
    let mut request = invoke("a");
    request["context"]["identity"]["ids"] = json!({"seriesImdb":"tt9999999"});
    let call = provider.handle(&request).remove(0);
    assert_eq!(
        call["request"]["url"],
        "https://api.tvmaze.com/lookup/shows?imdb=tt9999999"
    );
    assert_eq!(
        complete(&mut provider, &call, show())[0]["status"],
        "no_match"
    );
}
#[test]
fn manual_query_does_not_reuse_old_identifiers() {
    let mut provider = Provider::default();
    let mut request = invoke("a");
    request["context"]["manual"] = json!(true);
    request["context"]["query"] = json!("New Series");
    request["context"]["identity"]["ids"] = json!({"tvmazeShow":"111","seriesImdb":"tt1234567"});
    assert_eq!(
        provider.handle(&request)[0]["request"]["url"],
        "https://api.tvmaze.com/search/shows?q=New%20Series"
    );
}
#[test]
fn combined_and_nonseasonal_episodes_are_unsupported() {
    let mut provider = Provider::default();
    let mut request = invoke("a");
    request["context"]["identity"]["episodes"] = json!([1, 2]);
    assert_eq!(provider.handle(&request)[0]["status"], "unsupported");
    request = invoke("b");
    request["context"]["identity"]["episode"] = json!({"scheme":"absolute"});
    assert_eq!(provider.handle(&request)[0]["status"], "unsupported");
    assert!(provider.pending.is_empty());
}
#[test]
fn nested_seasonal_numbering_is_supported() {
    let mut provider = Provider::default();
    let mut request = invoke("a");
    request["context"]["identity"]
        .as_object_mut()
        .unwrap()
        .remove("season");
    request["context"]["identity"]
        .as_object_mut()
        .unwrap()
        .remove("episodes");
    request["context"]["identity"]["episode"] =
        json!({"scheme":"seasonal","season":1,"episodes":[2]});
    assert_eq!(provider.handle(&request)[0]["type"], "host-call");
}
#[test]
fn duplicate_seasons_do_not_select_the_first_entry() {
    let mut provider = Provider::default();
    let search = provider.handle(&invoke("a")).remove(0);
    let seasons = complete(&mut provider, &search, json!([{"show":show()}])).remove(0);
    assert_eq!(
        complete(
            &mut provider,
            &seasons,
            json!([{"id":1,"number":1},{"id":2,"number":1}])
        )[0]["status"],
        "ambiguous"
    );
}
#[test]
fn duplicate_episodes_do_not_select_the_first_entry() {
    let mut provider = Provider::default();
    let call = start_episodes(&mut provider, &invoke("a"));
    assert_eq!(
        complete(&mut provider, &call, json!([episode(), episode()]))[0]["status"],
        "ambiguous"
    );
}
#[test]
fn source_credit_cannot_use_an_unrelated_origin() {
    let mut provider = Provider::default();
    let call = start_episodes(&mut provider, &invoke("a"));
    let mut item = episode();
    item["url"] = json!("https://other.invalid/episode");
    assert_eq!(
        complete(&mut provider, &call, json!([item]))[0]["status"],
        "no_match"
    );
}
#[test]
fn cancel_and_wrong_invocation_ignore_late_results() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("a")).remove(0);
    assert!(provider
        .handle(&json!({"type":"host-result","id":call["id"],"invocation":"other","status":"ok"}))
        .is_empty());
    assert_eq!(provider.pending.len(), 1);
    provider.handle(&json!({"type":"cancel","id":"a"}));
    assert!(complete(&mut provider, &call, json!([{"show":show()}])).is_empty());
}
#[test]
fn rate_limit_has_a_bounded_retry_delay() {
    let mut provider = Provider::default();
    let call = provider.handle(&invoke("a")).remove(0);
    let result = provider
        .handle(
            &json!({"type":"host-result","id":call["id"],"invocation":"a","status":"ok",
        "response":{"status":429,"headers":{"Retry-After":"7"},"body":""}}),
        )
        .remove(0);
    assert_eq!(result["status"], "rate_limited");
    assert_eq!(result["data"]["retryAfterSeconds"], 7);
}
#[test]
fn malformed_json_and_body_limit_fail() {
    for body in ["broken".to_owned(), " ".repeat(MAX_BODY + 1)] {
        let mut provider = Provider::default();
        let call = provider.handle(&invoke("a")).remove(0);
        let result = provider
            .handle(
                &json!({"type":"host-result","id":call["id"],"invocation":"a","status":"ok",
            "response":{"status":200,"headers":{},"body":body}}),
            )
            .remove(0);
        assert_eq!(result["status"], "error");
    }
}
#[test]
fn active_request_count_has_a_limit() {
    let mut provider = Provider::default();
    for n in 0..MAX_PENDING {
        assert_eq!(
            provider.handle(&invoke(&format!("a{n}")))[0]["type"],
            "host-call"
        );
    }
    assert_eq!(
        provider.handle(&invoke("extra"))[0]["status"],
        "rate_limited"
    );
    assert_eq!(provider.pending.len(), MAX_PENDING);
}

#[test]
fn season_cache_serves_a_second_episode_without_more_http() {
    let mut provider = Provider::default();
    let call = start_episodes(&mut provider, &invoke("a"));
    let mut next = episode();
    next["number"] = json!(3);
    next["name"] = json!("Next episode");
    assert_eq!(
        complete(&mut provider, &call, json!([episode(), next]))[0]["status"],
        "match"
    );
    let mut request = invoke("b");
    request["context"]["identity"]["episodes"] = json!([3]);
    let result = provider.handle(&request);
    assert_eq!(result.len(), 1);
    assert_eq!(result[0]["type"], "result");
    assert_eq!(result[0]["data"]["fields"]["episodeTitle"], "Next episode");
    request["id"] = json!("c");
    request["context"]["identity"]["title"] = json!("Other Series");
    assert_eq!(provider.handle(&request)[0]["type"], "host-call");
    assert!(provider.cache.is_empty());
}
fn identify(id: &str, query: Option<&str>) -> Value {
    let mut message = invoke(id);
    message["operation"] = json!("identify");
    message["context"]["fields"] = json!([]);
    if let Some(query) = query {
        message["context"]["manual"] = json!(true);
        message["context"]["query"] = json!(query);
    }
    message
}
#[test]
fn identify_offers_each_matching_show_with_the_local_numbering() {
    let mut provider = Provider::default();
    let search = provider.handle(&identify("i", None)).remove(0);
    assert_eq!(
        search["request"]["url"],
        "https://api.tvmaze.com/search/shows?q=Example%20Series"
    );
    let other = json!({"id":112,"name":"Example Series","premiered":"2020-06-01","url":"https://www.tvmaze.com/shows/112/example"});
    let wrong_year = json!({"id":113,"name":"Example Series","premiered":"1999-01-01"});
    let reply = complete(
        &mut provider,
        &search,
        json!([{"show":show()},{"show":other},{"show":wrong_year}]),
    )
    .remove(0);
    assert_eq!(reply["status"], "match");
    let candidates = reply["data"]["candidates"].as_array().unwrap();
    assert_eq!(candidates.len(), 2);
    let first = &candidates[0]["identity"];
    assert_eq!(first["kind"], "episode");
    assert_eq!(first["season"], 1);
    assert_eq!(first["episodes"], json!([2]));
    assert_eq!(first["ids"]["tvmazeShow"], "111");
    assert_eq!(first["ids"]["seriesImdb"], "tt1234567");
    assert_eq!(candidates[1]["identity"]["ids"]["tvmazeShow"], "112");
    assert_eq!(
        candidates[1]["attribution"]["url"],
        "https://www.tvmaze.com/shows/112/example"
    );
    assert!(candidates[0].get("attribution").is_none());
}
#[test]
fn identify_uses_the_manual_query_and_limits_choices() {
    let mut provider = Provider::default();
    let search = provider
        .handle(&identify("i", Some("Another Show")))
        .remove(0);
    assert_eq!(
        search["request"]["url"],
        "https://api.tvmaze.com/search/shows?q=Another%20Show"
    );
    let shows: Vec<Value> = (1..=12)
        .map(|n| json!({"show":{"id":n,"name":format!("Another Show {n}")}}))
        .collect();
    let reply = complete(&mut provider, &search, json!(shows)).remove(0);
    assert_eq!(
        reply["data"]["candidates"].as_array().unwrap().len(),
        MAX_CANDIDATES
    );
    let search = provider.handle(&identify("j", None)).remove(0);
    assert_eq!(
        complete(&mut provider, &search, json!([])).remove(0)["status"],
        "no_match"
    );
}
#[test]
fn a_chosen_show_enriches_by_its_id() {
    let mut provider = Provider::default();
    let mut request = invoke("e");
    request["context"]["identity"]["ids"] = json!({"tvmazeShow":"112"});
    let call = provider.handle(&request).remove(0);
    assert_eq!(call["request"]["url"], "https://api.tvmaze.com/shows/112");
}
#[test]
fn identify_needs_one_numbered_episode() {
    let mut provider = Provider::default();
    let mut request = identify("i", None);
    request["context"]["identity"]["episodes"] = json!([2, 3]);
    assert_eq!(provider.handle(&request)[0]["status"], "unsupported");
}
