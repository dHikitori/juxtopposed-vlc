use super::*;
fn invoke(id: &str, identity: Value) -> Value {
    json!({"type":"invoke","id":id,"contribution":"artwork","operation":"enrich","context":{"identity":identity,"fields":["artwork"]}})
}
fn complete(provider: &mut Provider, call: &Value, body: &str) -> Vec<Value> {
    provider.handle(&json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],"status":"ok",
        "response":{"status":200,"body":body,"headers":{"content-type":"text/html"},"truncated":true}}))
}
#[test]
fn french_tv_keeps_all_channels_and_title_suffix_rules() {
    let mut provider = Provider::new(Kind::FrenchTv);
    let channels: Value = serde_json::from_str(include_str!("french-tv.json")).unwrap();
    assert_eq!(channels.as_object().unwrap().len(), 27);
    for title in ["France 2", " France 2 HD ", "France 2 (bas débit)"] {
        let result = provider
            .handle(&invoke("a", json!({"kind":"unknown","title":title})))
            .remove(0);
        assert_eq!(result["status"], "match");
        assert_eq!(
            result["data"]["records"][0]["fields"]["artwork"]["url"],
            channels["France 2"]
        );
        assert_eq!(result["data"]["records"][0]["evidence"]["type"], "label");
    }
    assert_eq!(
        provider.handle(&invoke("b", json!({"title":"Unknown channel"})))[0]["status"],
        "no_match"
    );
}
#[test]
fn lastfm_uses_bounded_html_and_accepts_current_meta_attribute_order() {
    let mut provider = Provider::new(Kind::LastFm);
    let call = provider.handle(&invoke("a", json!({"kind":"musicTrack","title":"Song","artist":"Artist","album":"Album (Deluxe) CD2"}))).remove(0);
    assert_eq!(
        call["request"]["url"],
        "https://www.last.fm/music/Artist/Album"
    );
    assert_eq!(call["request"]["responseBytes"], 65653);
    assert_eq!(call["request"]["allowTruncated"], true);
    let result = complete(&mut provider, &call, "<meta content='https://lastfm-img.freetls.fastly.net/i/u/300x300/fixture.jpg?a=1&amp;b=2' property='og:image'>").remove(0);
    assert_eq!(result["status"], "match");
    assert_eq!(
        result["data"]["records"][0]["fields"]["artwork"]["url"],
        "https://lastfm-img.freetls.fastly.net/i/u/300x300/fixture.jpg?a=1&b=2"
    );
    assert_eq!(
        result["data"]["records"][0]["identity"]["album"],
        "Album (Deluxe) CD2"
    );
    let call = provider
        .handle(&invoke(
            "bounded",
            json!({"artist":"Artist","album":"Album","title":"Song"}),
        ))
        .remove(0);
    let result = complete(&mut provider, &call, "<meta property='og:image' content='https://lastfm-img.freetls.fastly.net/i/u/ar0/fixture.jpg'>").remove(0);
    assert_eq!(
        result["data"]["records"][0]["fields"]["artwork"]["url"],
        "https://lastfm-img.freetls.fastly.net/i/u/500x500/fixture.jpg"
    );
}
#[test]
fn lastfm_rejects_private_and_unrelated_image_links() {
    for url in [
        "http://lastfm-img.freetls.fastly.net/image.jpg",
        "https://127.0.0.1/image.jpg",
        "https://lastfm-img.freetls.fastly.net.attacker.invalid/image.jpg",
        "https://lastfm-img.freetls.fastly.net@private/image.jpg",
        "https://lastfm-img.freetls.fastly.net/default_album_mega.png",
    ] {
        let mut provider = Provider::new(Kind::LastFm);
        let call = provider
            .handle(&invoke(
                "a",
                json!({"artist":"Artist","album":"Album","title":"Song"}),
            ))
            .remove(0);
        assert_eq!(
            complete(
                &mut provider,
                &call,
                &format!("<meta property=\"og:image\" content=\"{url}\" />")
            )[0]["status"],
            "no_match"
        );
    }
}
#[test]
fn google_preserves_multiple_image_choices_and_no_match_for_a_script_page() {
    let mut provider = Provider::new(Kind::GoogleImages);
    let call = provider
        .handle(&invoke(
            "a",
            json!({"artist":"Artist","album":"Album","title":"Song"}),
        ))
        .remove(0);
    let result = complete(&mut provider, &call, "<img height='150' src='https://encrypted-tbn0.gstatic.com/images?q=a'><img src='https://encrypted-tbn1.gstatic.com/images?q=b'>").remove(0);
    assert_eq!(result["data"]["records"].as_array().unwrap().len(), 2);
    let call = provider
        .handle(&invoke("b", json!({"artist":"Artist","title":"Song"})))
        .remove(0);
    assert_eq!(
        complete(
            &mut provider,
            &call,
            "<html><script>enablejs()</script></html>"
        )[0]["status"],
        "no_match"
    );
}
#[test]
fn asset_resolution_requires_an_issued_candidate() {
    let mut provider = Provider::new(Kind::FrenchTv);
    let mut request = invoke("a", json!({"title":"France 2"}));
    request["operation"] = json!("searchAssets");
    let result = provider.handle(&request).remove(0);
    let candidate = result["data"]["candidates"][0].clone();
    let mut resolve = json!({"type":"invoke","id":"b","contribution":"artwork","operation":"resolveAsset","context":{"candidate":candidate}});
    assert_eq!(provider.handle(&resolve)[0]["status"], "match");
    resolve["context"]["candidate"]["id"] = json!("forged");
    assert_eq!(provider.handle(&resolve)[0]["status"], "error");
}
#[test]
fn cancel_drops_late_results_and_wrong_invocation_does_not_consume_a_request() {
    let mut provider = Provider::new(Kind::LastFm);
    let call = provider
        .handle(&invoke("a", json!({"artist":"Artist","album":"Album"})))
        .remove(0);
    assert!(provider
        .handle(&json!({"type":"host-result","id":call["id"],"invocation":"forged"}))
        .is_empty());
    assert_eq!(provider.pending.len(), 1);
    provider.handle(&json!({"type":"cancel","id":"a"}));
    assert!(complete(&mut provider, &call, "").is_empty());
}
#[test]
fn html_size_and_active_request_limits_are_fixed() {
    let mut provider = Provider::new(Kind::LastFm);
    for n in 0..MAX_PENDING {
        assert_eq!(
            provider.handle(&invoke(
                &n.to_string(),
                json!({"artist":"Artist","album":"Album"})
            ))[0]["type"],
            "host-call"
        );
    }
    assert_eq!(
        provider.handle(&invoke("extra", json!({"artist":"Artist","album":"Album"})))[0]["status"],
        "rate_limited"
    );
    let mut provider = Provider::new(Kind::LastFm);
    let call = provider
        .handle(&invoke("a", json!({"artist":"Artist","album":"Album"})))
        .remove(0);
    assert_eq!(
        complete(&mut provider, &call, &"x".repeat(65654))[0]["status"],
        "error"
    );
}
fn music_complete(provider: &mut Provider, call: &Value, data: Value) -> Vec<Value> {
    provider.handle(
        &json!({"type":"host-result","id":call["id"],"invocation":call["invocation"],"status":"ok",
        "response":{"status":200,"body":data.to_string(),"truncated":false}}),
    )
}
const RELEASE: &str = "76df3287-6cda-33eb-8e9a-044b5e15ffdd";
fn music_identity() -> Value {
    json!({"kind":"musicTrack","title":"Song","artist":"Artist","album":"Album"})
}
fn music_release(id: &str, front: bool, code: &str) -> Value {
    json!({"id":id,"title":"Album","artist-credit":[{"name":"Artist"}],"cover-art-archive":{"front":front},"asin":code})
}
#[test]
fn musicbrainz_uses_scoped_release_ids_and_preserves_caa_and_amazon_assets() {
    for front in [false, true] {
        let mut provider = Provider::new(Kind::MusicBrainz);
        let mut identity = music_identity();
        identity["ids"] = json!({"musicBrainzAlbumId":RELEASE});
        let call = provider.handle(&invoke("a", identity)).remove(0);
        assert!(text(&call["request"], "url").contains(RELEASE));
        assert!(call["request"].get("allowTruncated").is_none());
        let reply = music_complete(
            &mut provider,
            &call,
            music_release(RELEASE, front, "B000001FI7"),
        )
        .remove(0);
        assert_eq!(reply["status"], "match");
        let record = &reply["data"]["records"][0];
        assert_eq!(record["evidence"]["type"], "identifier");
        let url = text(&record["fields"]["artwork"], "url");
        assert!(url.starts_with(if front {
            "https://coverartarchive.org/release/"
        } else {
            "https://images-na.ssl-images-amazon.com/images/P/"
        }));
    }
}
#[test]
fn musicbrainz_search_does_not_select_one_of_multiple_matching_releases() {
    let mut provider = Provider::new(Kind::MusicBrainz);
    let call = provider.handle(&invoke("a", music_identity())).remove(0);
    assert!(text(&call["request"], "url").contains("release/?query="));
    let second = "00000000-0000-0000-0000-000000000001";
    let detail = music_complete(
        &mut provider,
        &call,
        json!({"releases":[music_release(RELEASE,true,""),music_release(second,true,"")]}),
    )
    .remove(0);
    let next = music_complete(&mut provider, &detail, music_release(RELEASE, true, "")).remove(0);
    let reply = music_complete(&mut provider, &next, music_release(second, true, "")).remove(0);
    assert_eq!(reply["data"]["records"].as_array().unwrap().len(), 2);
    assert_eq!(reply["data"]["records"][0]["evidence"]["type"], "label");
}
#[test]
fn musicbrainz_rejects_changed_release_labels_and_unscoped_ids() {
    let mut provider = Provider::new(Kind::MusicBrainz);
    let mut identity = music_identity();
    identity["ids"] = json!({"musicBrainzAlbumId":RELEASE});
    let call = provider.handle(&invoke("a", identity.clone())).remove(0);
    let mut wrong = music_release(RELEASE, true, "");
    wrong["artist-credit"][0]["name"] = json!("Another artist");
    assert_eq!(
        music_complete(&mut provider, &call, wrong)[0]["status"],
        "no_match"
    );
    identity["ids"] = json!({"musicBrainzAlbumId":"../../private"});
    let call = provider.handle(&invoke("b", identity)).remove(0);
    assert!(text(&call["request"], "url").contains("?query="));
}
#[test]
fn musicbrainz_recording_lookup_and_issued_asset_resolution_work() {
    let mut provider = Provider::new(Kind::MusicBrainz);
    let mut identity = music_identity();
    identity.as_object_mut().unwrap().remove("album");
    let mut request = invoke("a", identity);
    request["operation"] = json!("searchAssets");
    let call = provider.handle(&request).remove(0);
    assert!(text(&call["request"], "url").contains("recording/?query="));
    let detail = music_complete(&mut provider,&call,json!({"recordings":[{"title":"Song","artist-credit":[{"name":"Artist"}],"releases":[{"id":RELEASE}]}]})).remove(0);
    let reply = music_complete(&mut provider, &detail, music_release(RELEASE, true, "")).remove(0);
    let candidate = reply["data"]["candidates"][0].clone();
    assert!(!text(&candidate, "id").is_empty());
    assert_eq!(provider.handle(&json!({"type":"invoke","id":"b","contribution":"artwork","operation":"resolveAsset","context":{"candidate":candidate}}))[0]["status"],"match");
}
