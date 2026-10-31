// SPDX-License-Identifier: GPL-2.0-or-later
//! Episodes: the player panel with the other episodes of the playing series.
//!
//! The app finds the episodes (media.siblings): the files of the same series
//! in its folder, or the entries of its podcast feed, as copies with opaque
//! IDs. This package groups them into seasons, episodes and versions, and
//! chooses what each row shows. A click asks the app to play an ID, to look
//! again, or to open the series choice; the app does the work.
use hakone_addon_message_v1::{
    contract::{result, Status},
    serde_json::{json, Map, Value},
    Addon, Incoming, Pending,
};

/// Rows for each "Show more" step.
pub const PAGE: u64 = 50;
const MAX_ITEMS: u64 = 100;

/// One file or feed entry, as the app gives it.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Sibling {
    pub id: String,
    pub kind: String,
    pub name: String,
    pub title: String,
    pub description: String,
    pub season: i64,
    pub episodes: Vec<i64>,
    pub absolute: String,
    pub air_date: String,
    pub version: i64,
    pub progress: f64,
    pub played: bool,
    pub current: bool,
    pub video: bool,
    pub has_source: bool,
    pub artwork: String,
}

impl Sibling {
    pub fn from(value: &Value) -> Option<Self> {
        let text = |key: &str| value.get(key).and_then(Value::as_str).unwrap_or("").to_owned();
        let flag = |key: &str| value.get(key).and_then(Value::as_bool).unwrap_or(false);
        let id = text("id");
        if id.is_empty() {
            return None;
        }
        Some(Self {
            id,
            kind: text("kind"),
            name: text("name"),
            title: text("title"),
            description: text("description"),
            season: value.get("season").and_then(Value::as_i64).unwrap_or(-1),
            episodes: value
                .get("episodes")
                .and_then(Value::as_array)
                .map(|list| list.iter().filter_map(Value::as_i64).collect())
                .unwrap_or_default(),
            absolute: text("absolute"),
            air_date: text("airDate"),
            version: value.get("version").and_then(Value::as_i64).unwrap_or(0),
            progress: value.get("progress").and_then(Value::as_f64).unwrap_or(0.0).clamp(0.0, 1.0),
            played: flag("played"),
            current: flag("current"),
            video: value.get("video").and_then(Value::as_bool).unwrap_or(true),
            has_source: flag("hasSource"),
            artwork: text("artwork"),
        })
    }
}

/// One media.siblings reply.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Listing {
    pub kind: String,
    pub show: String,
    pub message: String,
    pub loading: bool,
    pub limited: bool,
    pub total: u64,
    pub seasons: Vec<(i64, u64)>,
    pub current_season: Option<i64>,
    pub items: Vec<Sibling>,
}

impl Listing {
    pub fn from(response: &Map<String, Value>) -> Self {
        let text = |key: &str| response.get(key).and_then(Value::as_str).unwrap_or("").to_owned();
        Self {
            kind: text("kind"),
            show: text("show"),
            message: text("message"),
            loading: response.get("loading").and_then(Value::as_bool).unwrap_or(false),
            limited: response.get("limited").and_then(Value::as_bool).unwrap_or(false),
            total: response.get("total").and_then(Value::as_u64).unwrap_or(0),
            seasons: response
                .get("seasons")
                .and_then(Value::as_array)
                .map(|list| {
                    list.iter()
                        .filter_map(|row| Some((row.get("season")?.as_i64()?, row.get("count")?.as_u64()?)))
                        .collect()
                })
                .unwrap_or_default(),
            current_season: response.get("currentSeason").and_then(Value::as_i64),
            items: response
                .get("items")
                .and_then(Value::as_array)
                .map(|list| list.iter().filter_map(Sibling::from).collect())
                .unwrap_or_default(),
        }
    }
    /// The media has a series with at least one episode.
    pub fn available(&self) -> bool {
        (self.kind == "folder" || self.kind == "feed") && !self.seasons.is_empty()
    }
}

/// The versions of one episode follow each other, version 0 first.
pub fn group(items: &[Sibling]) -> Vec<Vec<Sibling>> {
    let mut episodes: Vec<Vec<Sibling>> = Vec::new();
    for item in items {
        match episodes.last_mut() {
            Some(last) if item.version > 0 => last.push(item.clone()),
            _ => episodes.push(vec![item.clone()]),
        }
    }
    episodes
}

pub fn season_label(season: i64) -> String {
    match season {
        0 => "Specials".into(),
        season if season < 0 => "Episodes".into(),
        season => format!("Season {season}"),
    }
}

/// "3", "3–4", an absolute number, an air date, or nothing.
pub fn number(item: &Sibling) -> String {
    if !item.episodes.is_empty() {
        return item.episodes.iter().map(i64::to_string).collect::<Vec<_>>().join("–");
    }
    if !item.absolute.is_empty() {
        return item.absolute.clone();
    }
    item.air_date.clone()
}

pub fn title(item: &Sibling) -> String {
    if !item.title.trim().is_empty() {
        return item.title.clone();
    }
    match number(item) {
        number if number.is_empty() => "Episode".into(),
        number => format!("Episode {number}"),
    }
}

/// What the user chose in the panel. The app keeps it in the view state.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Choice {
    pub revision: f64,
    pub season: Option<i64>,
    pub pages: u64,
    /// The expanded episode key; empty follows the playing episode and
    /// "collapsed" closes every row.
    pub expanded: String,
    pub version_episode: String,
    pub version: String,
}

impl Choice {
    pub fn from(state: &Map<String, Value>) -> Self {
        let text = |key: &str| state.get(key).and_then(Value::as_str).unwrap_or("").to_owned();
        Self {
            revision: state.get("revision").and_then(Value::as_f64).unwrap_or(-1.0),
            season: state.get("season").and_then(Value::as_i64).filter(|_| state.get("status").and_then(Value::as_str) == Some("ready")),
            pages: state.get("pages").and_then(Value::as_u64).unwrap_or(1).clamp(1, MAX_ITEMS / PAGE),
            expanded: text("expanded"),
            version_episode: text("versionEpisode"),
            version: text("version"),
        }
    }
    fn reset(revision: f64) -> Self {
        Self { revision, pages: 1, ..Self::default() }
    }
}

/// The view state for one season of the listing.
pub fn render(listing: &Listing, choice: &Choice, season: i64) -> Value {
    let feed = listing.kind == "feed";
    let episodes = group(&listing.items);
    // The playing episode is open unless the user chose another or none.
    let open = |key: &str, versions: &[Sibling]| match choice.expanded.as_str() {
        "" => versions.iter().any(|item| item.current),
        "collapsed" => false,
        expanded => expanded == key,
    };
    let rows: Vec<Value> = episodes
        .iter()
        .map(|versions| {
            let first = &versions[0];
            let key = first.id.clone();
            // Files: each version is a choice. Feeds: version 1 is the video.
            let choices: Vec<&Sibling> = if feed { vec![first] } else { versions.iter().collect() };
            let chosen = choices
                .iter()
                .find(|item| choice.version_episode == key && item.id == choice.version)
                .or_else(|| choices.iter().find(|item| item.current))
                .copied()
                .unwrap_or(first);
            let current = versions.iter().any(|item| item.current);
            let played = versions.iter().any(|item| item.played);
            let progress = versions.iter().map(|item| item.progress).fold(0.0, f64::max);
            // The playing episode is marked; a finished one says so quietly.
            let mark = if current { "Now playing" } else { "" };
            let subtitle = if !current && played { if feed { "Listened" } else { "Watched" } } else { "" };
            let playing = chosen.current;
            let label = if playing { "Playing" } else if chosen.progress > 0.0 { "Resume" } else { "Play" };
            let video = if feed { versions.iter().find(|item| item.version == 1) } else { None };
            let artwork = versions.iter().find(|item| !item.artwork.is_empty()).map(|item| item.artwork.clone()).unwrap_or_default();
            json!({
                "key": key, "number": number(first), "title": title(first), "mark": mark, "subtitle": subtitle, "progress": progress,
                "expanded": open(&key, versions), "artwork": artwork,
                "description": first.description, "hasDescription": !first.description.trim().is_empty(),
                "versions": choices.iter().map(|item| json!({"key": item.id, "label": if item.name.is_empty() { title(item) } else { item.name.clone() }})).collect::<Vec<_>>(),
                "version": chosen.id, "multiple": choices.len() > 1,
                "fileLabel": chosen.name, "hasFile": !chosen.name.is_empty(),
                "playKey": chosen.id, "playLabel": label, "playEnabled": !playing,
                "videoKey": video.map(|item| item.id.clone()).unwrap_or_default(), "hasVideo": video.is_some(),
                "sourceKey": versions.iter().find(|item| item.has_source).map(|item| item.id.clone()).unwrap_or_default(),
                "hasSource": versions.iter().any(|item| item.has_source)
            })
        })
        .collect();
    let notice = if !listing.message.is_empty() {
        listing.message.clone()
    } else if listing.loading {
        "Finding episodes…".into()
    } else if listing.limited {
        "Some folders could not be fully searched. Refresh to try again, or choose the series folder.".into()
    } else {
        String::new()
    };
    let shown = listing.items.len() as u64;
    json!({
        "status": "ready", "revision": choice.revision, "kind": listing.kind, "files": listing.kind == "folder",
        "show": if listing.show.is_empty() { "Episodes".to_owned() } else { listing.show.clone() },
        "notice": notice, "hasNotice": !notice.is_empty(),
        "seasons": listing.seasons.iter().map(|(season, _)| json!({"key": season, "label": season_label(*season)})).collect::<Vec<_>>(),
        "manySeasons": listing.seasons.len() > 1, "season": season, "seasonLabel": season_label(season),
        "pages": choice.pages, "more": shown < listing.total && shown < MAX_ITEMS,
        "expanded": choice.expanded, "versionEpisode": choice.version_episode, "version": choice.version,
        "episodes": rows
    })
}

/// The state when the media has no series.
fn empty(revision: f64) -> Value {
    json!({"status": "none", "revision": revision, "kind": "none", "files": false, "show": "Episodes",
           "notice": "This media has no other episodes.", "hasNotice": true, "seasons": [], "manySeasons": false,
           "season": -1, "seasonLabel": "", "pages": 1, "more": false, "expanded": "", "versionEpisode": "",
           "version": "", "episodes": []})
}

/// The season to show: the chosen one, else the playing one, else the first.
pub fn season(listing: &Listing, choice: &Choice) -> Option<i64> {
    let known = |season: i64| listing.seasons.iter().any(|(value, _)| *value == season);
    choice
        .season
        .filter(|season| known(*season))
        .or(listing.current_season.filter(|season| known(*season)))
        .or(listing.seasons.first().map(|(season, _)| *season))
}

enum Step {
    /// Find the seasons; then read one, or answer whether there is any.
    Seasons,
    /// Read one season.
    Season(i64),
    /// Ask for the artwork of the open row; the reply is not used.
    Artwork(Value),
}

struct Call {
    invocation: String,
    sessionless: bool,
    choice: Choice,
    step: Step,
    retried: bool,
}

pub struct Episodes {
    pending: Pending<Call>,
    sequence: u64,
}

impl Default for Episodes {
    fn default() -> Self {
        Self { pending: Pending::new(32), sequence: 0 }
    }
}

fn effect(id: &str, name: &str, value: Value) -> Vec<Value> {
    vec![result(id, Status::Match, json!({"effect": {name: value}}))]
}

impl Episodes {
    fn read(&mut self, call: Call, request: Value) -> Vec<Value> {
        self.sequence += 1;
        let id = format!("siblings-{}", self.sequence);
        let invocation = call.invocation.clone();
        if self.pending.insert(&id, &invocation, call).is_err() {
            return vec![result(&invocation, Status::Error, json!({}))];
        }
        vec![json!({"type": "host-call", "id": id, "invocation": invocation, "operation": "media.siblings", "request": request})]
    }
    fn read_season(&mut self, mut call: Call, season: i64) -> Vec<Value> {
        let limit = (call.choice.pages * PAGE).min(MAX_ITEMS);
        let expanded = call.choice.expanded.clone();
        let artwork: Vec<&str> = [expanded.as_str()].into_iter().filter(|key| !key.is_empty() && *key != "collapsed").collect();
        call.step = Step::Season(season);
        self.read(call, json!({"season": season, "offset": 0, "limit": limit, "artwork": artwork}))
    }

    fn invoke(&mut self, id: &str, operation: &str, context: &Map<String, Value>) -> Vec<Value> {
        let empty_map = Map::new();
        let arguments = context.get("arguments").and_then(Value::as_object).unwrap_or(&empty_map);
        let text = |key: &str| arguments.get(key).and_then(Value::as_str).unwrap_or("").to_owned();
        let state = context.get("state").and_then(Value::as_object);
        let revision = context.get("media").and_then(|media| media.get("revision")).and_then(Value::as_f64);
        let mut choice = state.map(Choice::from).unwrap_or_else(|| Choice::reset(revision.unwrap_or(-1.0)));
        let call = |choice: Choice, sessionless: bool| Call { invocation: id.to_owned(), sessionless, choice, step: Step::Seasons, retried: false };
        match operation {
            "context" => {
                if state.is_none() {
                    return self.read(call(choice, true), json!({"limit": 1}));
                }
                // New media: start again at the playing episode's season.
                if let Some(revision) = revision.filter(|revision| *revision != choice.revision) {
                    choice = Choice::reset(revision);
                }
                match choice.season {
                    Some(season) => self.read_season(call(choice, false), season),
                    None => self.read(call(choice, false), json!({"limit": 1})),
                }
            }
            "season" => match arguments.get("season").and_then(Value::as_i64) {
                Some(season) => {
                    choice = Choice { season: Some(season), pages: 1, expanded: String::new(), ..choice };
                    self.read_season(call(choice, false), season)
                }
                None => vec![result(id, Status::Error, json!({}))],
            },
            "expand" => {
                let key = text("key");
                if key.is_empty() {
                    return vec![result(id, Status::Error, json!({}))];
                }
                let version = text("version");
                if version.is_empty() {
                    // The view sends whether the row is open now.
                    let open = arguments.get("open").and_then(Value::as_bool).unwrap_or(choice.expanded == key);
                    choice.expanded = if open { "collapsed".into() } else { key };
                } else {
                    choice.expanded = key.clone();
                    choice.version_episode = key;
                    choice.version = version;
                }
                match choice.season {
                    Some(season) => self.read_season(call(choice, false), season),
                    None => self.read(call(choice, false), json!({"limit": 1})),
                }
            }
            "more" => {
                choice.pages = (choice.pages + 1).min(MAX_ITEMS / PAGE);
                match choice.season {
                    Some(season) => self.read_season(call(choice, false), season),
                    None => self.read(call(choice, false), json!({"limit": 1})),
                }
            }
            "play" if !text("key").is_empty() => effect(
                id,
                "play",
                json!({"id": text("key"), "fromStart": arguments.get("fromStart").and_then(Value::as_bool).unwrap_or(false)}),
            ),
            "source" if !text("key").is_empty() => effect(id, "source", json!({"id": text("key")})),
            "refresh" => effect(id, "rescan", json!({"chooseFolder": arguments.get("folder").and_then(Value::as_bool).unwrap_or(false)})),
            "identify" => effect(id, "identify", json!({})),
            _ => vec![result(id, Status::Unsupported, json!({}))],
        }
    }

    fn complete(&mut self, id: &str, invocation: &str, ok: bool, response: Option<&Map<String, Value>>) -> Vec<Value> {
        let Some(mut call) = self.pending.take(id, invocation) else {
            return vec![];
        };
        let invocation = call.invocation.clone();
        if let Step::Artwork(state) = call.step {
            return vec![result(&invocation, Status::Match, json!({"available": true, "state": state}))];
        }
        let Some(response) = response.filter(|_| ok) else {
            return vec![result(&invocation, Status::Error, json!({}))];
        };
        let listing = Listing::from(response);
        if !listing.available() {
            if call.sessionless {
                return vec![result(&invocation, Status::Match, json!({"available": false}))];
            }
            return vec![result(&invocation, Status::Match, json!({"available": false, "state": empty(call.choice.revision)}))];
        }
        if call.sessionless {
            return vec![result(&invocation, Status::Match, json!({"available": true}))];
        }
        let Some(season) = season(&listing, &call.choice) else {
            return vec![result(&invocation, Status::Match, json!({"available": false, "state": empty(call.choice.revision)}))];
        };
        match call.step {
            Step::Season(read) if read == season => {
                let state = render(&listing, &call.choice, season);
                // The playing episode opens by itself: ask once for its artwork.
                let open = state["episodes"].as_array().and_then(|rows| {
                    rows.iter().find(|row| row["expanded"] == true && row["artwork"] == "").map(|row| row["key"].clone())
                });
                match open.filter(|_| call.choice.expanded.is_empty()) {
                    Some(key) => {
                        call.step = Step::Artwork(state);
                        self.read(call, json!({"season": season, "limit": 1, "artwork": [key]}))
                    }
                    None => vec![result(&invocation, Status::Match, json!({"available": true, "state": state}))],
                }
            }
            // The chosen season is gone, or the seasons were not known yet.
            _ if !call.retried => {
                call.retried = !matches!(call.step, Step::Seasons);
                call.choice.season = Some(season);
                self.read_season(call, season)
            }
            _ => vec![result(&invocation, Status::Error, json!({}))],
        }
    }
}

/// Sample data for the install self-test: two versions of one episode, a
/// second episode, a special.
pub fn sample() -> Listing {
    let item = |id: &str, season: i64, episode: i64, version: i64, current: bool| Sibling {
        id: id.into(),
        kind: "folder".into(),
        name: format!("{id}.mkv"),
        season,
        episodes: vec![episode],
        version,
        current,
        ..Sibling::default()
    };
    Listing {
        kind: "folder".into(),
        show: "Show".into(),
        total: 3,
        seasons: vec![(0, 1), (1, 2)],
        current_season: Some(1),
        items: vec![item("a", 1, 1, 0, false), item("b", 1, 1, 1, true), item("c", 1, 2, 0, false)],
        ..Listing::default()
    }
}

impl Addon for Episodes {
    fn self_test(&mut self) -> bool {
        let listing = sample();
        let state = render(&listing, &Choice::reset(1.0), 1);
        let rows = state["episodes"].as_array().cloned().unwrap_or_default();
        rows.len() == 2
            && rows[0]["multiple"] == true
            && rows[0]["mark"] == "Now playing"
            && rows[0]["playKey"] == "b"
            && rows[0]["expanded"] == true
            && state["seasons"][0]["label"] == "Specials"
    }
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value> {
        match message {
            Incoming::Invoke { id, operation, context, .. } => self.invoke(id, operation, context),
            Incoming::HostResult { id, invocation, ok, response, .. } => self.complete(id, invocation, ok, response),
            Incoming::Cancel { id, .. } => {
                self.pending.cancel(id);
                vec![]
            }
        }
    }
}

hakone_addon_message_v1::export_addon!(Episodes, Episodes::default());

#[cfg(test)]
mod tests;
