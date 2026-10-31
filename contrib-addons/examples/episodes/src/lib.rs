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

/// Items on one page. Each dispatch has a fixed instruction budget, and a
/// row costs about 120,000 instructions, mostly in allocation: 40 rows use
/// about half of it. Longer seasons page with Next and Previous.
pub const PAGE: u64 = 40;

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
    /// The playing item's index in the season, and this page's first index.
    pub current_offset: Option<u64>,
    pub offset: u64,
    pub items: Vec<Sibling>,
    /// IDs whose details (description, artwork, source) the reply had.
    pub detailed: Vec<String>,
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
            current_offset: response.get("currentOffset").and_then(Value::as_u64),
            offset: response.get("offset").and_then(Value::as_u64).unwrap_or(0),
            items: response
                .get("items")
                .and_then(Value::as_array)
                .map(|list| list.iter().filter_map(Sibling::from).collect())
                .unwrap_or_default(),
            detailed: Vec::new(),
        }
        .with_details(response)
    }
    /// Details come only for the IDs a read asked for.
    pub fn with_details(mut self, response: &Map<String, Value>) -> Self {
        for detail in response.get("details").and_then(Value::as_array).into_iter().flatten() {
            let Some(id) = detail.get("id").and_then(Value::as_str) else { continue };
            if let Some(item) = self.items.iter_mut().find(|item| item.id == id) {
                item.description = detail.get("description").and_then(Value::as_str).unwrap_or("").to_owned();
                item.artwork = detail.get("artwork").and_then(Value::as_str).unwrap_or("").to_owned();
                item.has_source = detail.get("hasSource").and_then(Value::as_bool).unwrap_or(false);
            }
            self.detailed.push(id.to_owned());
        }
        self
    }
    /// The media has a series with at least one episode.
    pub fn available(&self) -> bool {
        (self.kind == "folder" || self.kind == "feed") && !self.seasons.is_empty()
    }
}

/// The versions of one episode follow each other, version 0 first.
pub fn group(items: &[Sibling]) -> Vec<&[Sibling]> {
    let mut episodes = Vec::new();
    let mut start = 0;
    for index in 1..=items.len() {
        if index == items.len() || items[index].version == 0 {
            if start < index {
                episodes.push(&items[start..index]);
            }
            start = index;
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
    /// The chosen page; None shows the playing episode's page.
    pub page: Option<u64>,
    /// The expanded episode key; empty follows the playing episode and
    /// "collapsed" closes every row.
    pub expanded: String,
    pub version_episode: String,
    pub version: String,
    /// The season list replaces the episodes (the header button).
    pub seasons_view: bool,
}

impl Choice {
    pub fn from(state: &Map<String, Value>) -> Self {
        let text = |key: &str| state.get(key).and_then(Value::as_str).unwrap_or("").to_owned();
        Self {
            revision: state.get("revision").and_then(Value::as_f64).unwrap_or(-1.0),
            season: state.get("season").and_then(Value::as_i64).filter(|_| state.get("status").and_then(Value::as_str) == Some("ready")),
            page: state.get("page").and_then(Value::as_u64).filter(|page| *page <= 250),
            expanded: text("expanded"),
            version_episode: text("versionEpisode"),
            version: text("version"),
            seasons_view: state.get("showingSeasons").and_then(Value::as_bool).unwrap_or(false),
        }
    }
    pub fn reset(revision: f64) -> Self {
        Self { revision, ..Self::default() }
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
    // A dispatch has a fixed instruction budget: a closed row carries only
    // what the row shows, and the open row adds its details.
    let rows: Vec<Value> = episodes
        .iter()
        .map(|versions| {
            let first = &versions[0];
            let key = &first.id;
            // Files: each version is a choice. Feeds: version 1 is the video.
            let choices = if feed { &versions[..1] } else { *versions };
            let chosen = choices
                .iter()
                .find(|item| choice.version_episode == *key && item.id == choice.version)
                .or_else(|| choices.iter().find(|item| item.current))
                .unwrap_or(first);
            let current = versions.iter().any(|item| item.current);
            let played = versions.iter().any(|item| item.played);
            let progress = versions.iter().map(|item| item.progress).fold(0.0, f64::max);
            // The playing episode is marked; a finished one says so quietly.
            let mark = if current { "Now playing" } else { "" };
            let subtitle = if !current && played { if feed { "Listened" } else { "Watched" } } else { "" };
            let playing = chosen.current;
            let label = if playing { "Playing" } else if chosen.progress > 0.0 { "Resume" } else { "Play" };
            let expanded = open(key, versions);
            let mut row = json!({"key": key, "title": title(first), "playKey": chosen.id, "playLabel": label});
            if let Some(row) = row.as_object_mut() {
                // Empty values stay out: a missing text binding shows nothing.
                let number = number(first);
                for (name, value) in [("number", number.as_str()), ("mark", mark), ("subtitle", subtitle)] {
                    if !value.is_empty() {
                        row.insert(name.into(), value.into());
                    }
                }
                if progress > 0.0 {
                    row.insert("progress".into(), progress.into());
                }
                if current {
                    row.insert("current".into(), true.into());
                }
            }
            if expanded {
                row["expanded"] = true.into();
                let video = if feed { versions.iter().find(|item| item.version == 1) } else { None };
                let source = versions.iter().find(|item| item.has_source);
                let details = json!({
                    "artwork": versions.iter().find(|item| !item.artwork.is_empty()).map(|item| item.artwork.as_str()).unwrap_or(""),
                    "description": first.description, "hasDescription": !first.description.trim().is_empty(),
                    "versions": choices.iter().map(|item| json!({"key": item.id, "label": if item.name.is_empty() { title(item) } else { item.name.clone() }})).collect::<Vec<_>>(),
                    "version": chosen.id, "multiple": choices.len() > 1,
                    "fileLabel": chosen.name, "hasFile": !chosen.name.is_empty(), "playEnabled": !playing,
                    "videoKey": video.map(|item| item.id.as_str()).unwrap_or(""), "hasVideo": video.is_some(),
                    "sourceKey": source.map(|item| item.id.as_str()).unwrap_or(""), "hasSource": source.is_some()
                });
                if let (Some(row), Value::Object(details)) = (row.as_object_mut(), details) {
                    row.extend(details);
                }
            }
            row
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
    let page = choice.page.unwrap_or(listing.offset / PAGE);
    // The header shows the show and season; with several seasons it opens
    // the season list, which marks the playing episode's season.
    let many = listing.seasons.len() > 1;
    let showing = many && choice.seasons_view;
    // While a scan runs, an empty list waits instead of saying "none found".
    let episodes_shown = !showing && (!rows.is_empty() || (!listing.loading && notice.is_empty()));
    json!({
        "status": "ready", "revision": choice.revision, "kind": listing.kind, "files": listing.kind == "folder",
        "show": if listing.show.is_empty() { "Episodes".to_owned() } else { listing.show.clone() },
        "notice": notice, "hasNotice": !notice.is_empty(),
        "seasonRows": listing.seasons.iter().map(|(value, _)| json!({"key": value, "label": season_label(*value),
            "icon": if listing.current_season == Some(*value) { "check" } else { "" },
            "current": listing.current_season == Some(*value)})).collect::<Vec<_>>(),
        "showingSeasons": showing, "seasonsClosed": many && !showing, "oneSeason": !many,
        "headerSubtitle": if showing { String::new() } else { season_label(season) },
        "season": season, "seasonLabel": season_label(season), "episodesShown": episodes_shown,
        "page": page, "more": listing.offset + shown < listing.total, "less": listing.offset > 0 && !showing,
        "expanded": choice.expanded, "versionEpisode": choice.version_episode, "version": choice.version,
        "episodes": rows
    })
}

/// The key of the open episode: the chosen one, else the playing one.
pub fn open_key(listing: &Listing, choice: &Choice) -> Option<String> {
    match choice.expanded.as_str() {
        "collapsed" => None,
        "" => group(&listing.items).into_iter().find(|versions| versions.iter().any(|item| item.current)).map(|versions| versions[0].id.clone()),
        key => listing.items.iter().any(|item| item.id == key).then(|| key.to_owned()),
    }
}

/// The state when the media has no series.
fn empty(revision: f64) -> Value {
    json!({"status": "none", "revision": revision, "kind": "none", "files": false, "show": "Episodes",
           "notice": "This media has no other episodes.", "hasNotice": true, "seasonRows": [], "showingSeasons": false,
           "seasonsClosed": false, "oneSeason": true, "headerSubtitle": "", "episodesShown": false,
           "season": -1, "seasonLabel": "", "page": 0, "more": false, "less": false, "expanded": "", "versionEpisode": "",
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
    /// Read the details of the open row, then answer.
    Details(Listing, i64),
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
        let offset = call.choice.page.unwrap_or(0) * PAGE;
        let expanded = call.choice.expanded.clone();
        let details: Vec<&str> = [expanded.as_str()].into_iter().filter(|key| !key.is_empty() && *key != "collapsed").collect();
        call.step = Step::Season(season);
        self.read(call, json!({"season": season, "offset": offset, "limit": PAGE, "details": details}))
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
                    choice = Choice { season: Some(season), page: None, expanded: String::new(), seasons_view: false, ..choice };
                    self.read_season(call(choice, false), season)
                }
                None => vec![result(id, Status::Error, json!({}))],
            },
            // The header button: the season list, or back to the episodes.
            "seasons" => {
                choice.seasons_view = !choice.seasons_view;
                match choice.season {
                    Some(season) => self.read_season(call(choice, false), season),
                    None => self.read(call(choice, false), json!({"limit": 1})),
                }
            }
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
            "more" | "less" => {
                let page = state.and_then(|state| state.get("page")).and_then(Value::as_u64).unwrap_or(0);
                choice.page = Some(if operation == "more" { (page + 1).min(250) } else { page.saturating_sub(1) });
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
        let Some(response) = response.filter(|_| ok) else {
            return vec![result(&invocation, Status::Error, json!({}))];
        };
        if let Step::Details(listing, season) = call.step {
            let state = render(&listing.with_details(response), &call.choice, season);
            return vec![result(&invocation, Status::Match, json!({"available": true, "state": state}))];
        }
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
            // The playing episode is on another page: show that page.
            Step::Season(read)
                if read == season && call.choice.page.is_none() && !call.retried
                    && listing.current_offset.is_some_and(|offset| offset / PAGE != listing.offset / PAGE) =>
            {
                call.retried = true;
                call.choice.page = listing.current_offset.map(|offset| offset / PAGE);
                self.read_season(call, season)
            }
            Step::Season(read) if read == season => {
                // The playing episode is open by itself: read its details first.
                match open_key(&listing, &call.choice).filter(|key| !listing.detailed.contains(key)) {
                    Some(key) => {
                        call.step = Step::Details(listing, season);
                        self.read(call, json!({"season": season, "limit": 1, "details": [key]}))
                    }
                    None => vec![result(&invocation, Status::Match, json!({"available": true, "state": render(&listing, &call.choice, season)}))],
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
            && state["seasonRows"][0]["label"] == "Specials"
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
