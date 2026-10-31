// SPDX-License-Identifier: GPL-2.0-or-later
//! A discovery source template with a fixed catalogue.
//!
//! Replace the catalogue with requests to your service (see the metadata
//! template for a network.http call). The host shows the pages, asks for
//! suggestions to keep the queue filled, and checks an open folder again
//! through subscribe.
use hakone_addon_message_v1::{
    contract::{result, Item, ItemKind, Page, Status},
    serde_json::{json, Map, Value},
    Addon, Incoming,
};

/// (folder, id, title) rows; an empty folder is the root.
const CATALOGUE: &[(&str, &str, &str)] = &[
    ("", "news", "News"),
    ("", "music", "Music"),
    ("news", "news-1", "Morning news"),
    ("news", "news-2", "Evening news"),
    ("music", "music-1", "Quiet piano"),
    ("music", "music-2", "Jazz evening"),
];
/// Change it when the catalogue changes, so that subscribe can report it.
const REVISION: &str = "catalogue-1";

fn rows(folder: &str) -> Vec<Item> {
    CATALOGUE
        .iter()
        .filter(|(parent, _, _)| *parent == folder)
        .map(|(_, id, title)| {
            let folder = CATALOGUE.iter().any(|(parent, _, _)| parent == id);
            let mut item = Item::new(id, if folder { ItemKind::Node } else { ItemKind::Media }, title);
            if !folder {
                item.media_kind = Some("audio".into());
            }
            item
        })
        .collect()
}

fn page(id: &str, items: Vec<Item>, revision: bool) -> Vec<Value> {
    let page = Page {
        items,
        revision: revision.then(|| REVISION.to_string()),
        next_check_seconds: revision.then_some(600),
        ..Page::default()
    };
    match page.to_value() {
        Some(data) => vec![result(id, Status::Match, data)],
        None => vec![result(id, Status::Error, json!({}))],
    }
}

#[derive(Default)]
pub struct Source;

impl Source {
    fn invoke(&mut self, id: &str, operation: &str, context: &Map<String, Value>) -> Vec<Value> {
        let text = |key: &str| context.get(key).and_then(Value::as_str).unwrap_or("");
        match operation {
            "roots" => page(id, rows(""), true),
            "browse" => page(id, rows(text("nodeRef")), true),
            "search" => {
                let query = text("query").to_lowercase();
                let found = CATALOGUE
                    .iter()
                    .filter(|(parent, _, title)| !parent.is_empty() && title.to_lowercase().contains(&query))
                    .map(|(_, id, title)| Item::new(id, ItemKind::Media, title))
                    .collect();
                page(id, found, false)
            }
            "suggest" => {
                let limit = context.get("limit").and_then(Value::as_u64).unwrap_or(5) as usize;
                let items = CATALOGUE
                    .iter()
                    .filter(|(parent, _, _)| !parent.is_empty())
                    .take(limit)
                    .map(|(_, id, title)| Item::new(id, ItemKind::Media, title))
                    .collect();
                page(id, items, false)
            }
            // No change since the revision the host has: say so.
            "subscribe" if text("revision") == REVISION => {
                vec![result(id, Status::NoMatch, json!({"revision": REVISION, "nextCheckSeconds": 600}))]
            }
            "subscribe" => page(id, rows(text("collectionRef")), true),
            _ => vec![result(id, Status::Unsupported, json!({}))],
        }
    }
}

impl Addon for Source {
    fn self_test(&mut self) -> bool {
        !rows("").is_empty() && rows("").iter().all(|item| item.to_value().is_some())
    }
    fn handle(&mut self, message: Incoming<'_>) -> Vec<Value> {
        match message {
            Incoming::Invoke { id, operation, context, .. } => self.invoke(id, operation, context),
            Incoming::HostResult { .. } | Incoming::Cancel { .. } => vec![],
        }
    }
}

hakone_addon_message_v1::export_addon!(Source, Source::default());

#[cfg(test)]
mod tests {
    use super::*;

    fn context(value: Value) -> Map<String, Value> {
        value.as_object().unwrap().clone()
    }

    #[test]
    fn pages_folders_search_and_suggestions() {
        let mut source = Source;
        let roots = source.invoke("a", "roots", &context(json!({})));
        assert_eq!(roots[0]["data"]["items"][0]["kind"], "node");
        assert_eq!(roots[0]["data"]["revision"], REVISION);
        let folder = source.invoke("b", "browse", &context(json!({"nodeRef": "news"})));
        assert_eq!(folder[0]["data"]["items"].as_array().unwrap().len(), 2);
        let found = source.invoke("c", "search", &context(json!({"query": "piano"})));
        assert_eq!(found[0]["data"]["items"][0]["id"], "music-1");
        let suggested = source.invoke("d", "suggest", &context(json!({"limit": 2})));
        assert_eq!(suggested[0]["data"]["items"].as_array().unwrap().len(), 2);
        let unchanged = source.invoke("e", "subscribe", &context(json!({"collectionRef": "", "revision": REVISION})));
        assert_eq!(unchanged[0]["status"], "no_match");
        assert!(Source.self_test());
    }
}
