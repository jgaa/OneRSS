# OneRSS Initial Specification (Structured Draft)

## Overview

OneRSS is a traditional desktop-style RSS reader with a synchronized backend. The user interface and workflow should initially be modeled after **Akregator**, with additional functionality added where it provides clear value.

The philosophy is to build the smallest useful implementation first. New protocol messages, database operations, and features are only added when they are actually needed.

---

# Goals

The application should:

* behave like a classic RSS reader
* synchronize state across multiple devices
* use a native Qt/QML desktop application rather than a web UI
* use a simple custom backend instead of HTTP/gRPC
* prioritize simplicity, security, and maintainability

---

# User Interface

The application uses a classic three-pane layout.

```
+----------------------+--------------------------------------+
|                      | Article List                         |
| Feed Tree            |--------------------------------------|
|                      | Article Preview                      |
|                      |                                      |
+----------------------+--------------------------------------+
```

---

# Left Pane — Feed Tree

The feed tree behaves similarly to Akregator.

## Folder nodes

Display:

* folder icon

Context menu:

* Add feed
* Add folder
* Rename
* Delete

---

## Feed nodes

Display:

* website favicon (or site icon)

Context menu:

* Fetch feed
* Open home page
* Edit feed
* Delete feed
* Mark feed as read

---

# Feed Properties

Each feed stores:

* Name
* Feed URL
* Comment
* Update interval

  * use system default
  * custom interval
* Notify when new articles arrive
* Archive policy
* Advanced options
* Tags

---

## Notifications

Optional per feed.

Desktop:

* native desktop notification

Android:

* standard Android notification

---

## Archive Policy

Support the same options as Akregator.

* Use system default
* Keep all articles
* Keep last *N* articles
* Keep articles newer than *N* days
* Disable local archive (only display articles currently present in the RSS feed)

---

## Advanced Options

Initially mirror Akregator.

Examples:

* Load full web page while reading
* Mark articles as read when they arrive

---

## Feed Tags

Feeds may have arbitrary tags.

Examples:

* IT News
* Cybersecurity
* Programming
* Linux

Tags are independent of the folder hierarchy.

Users organize feeds:

* by folders
* by tags

A tag-based view will be added later.

---

# Right Pane

The right side is horizontally split.

## Top — Article List

Contains:

* search box
* article list

Search initially searches article titles.

---

## Bottom — Article Preview

Shows:

* title
* publication date
* feed content

Rendering rules:

* simple text rendering
* ignore CSS
* no JavaScript
* no remote resources
* no embedded icons
* no active content

Think of the preview as a Markdown-like rendering of the RSS content.

---

## Opening Articles

Double-click:

* open in default browser profile

Users may define multiple browser profiles.

Examples:

* Firefox
* Firefox (No JavaScript)
* Firefox Private
* Konqueror

Each profile contains:

* display name
* command line
* optional arguments

One profile is marked as the default.

Right-clicking an article allows selecting which browser profile to use.

---

## "Complete Story"

Support the equivalent of Akregator's "Complete Story" action.

---

# Backend Architecture

Unlike a traditional desktop RSS reader, all persistent state lives on the server.

The desktop application behaves as a synchronized client.

The client caches data only while running.

---

# Sign-up

Sign-up occurs directly through the application.

There is no HTTP interface.

Transport:

* TLS
* preferably mTLS

Server listens on two ports:

* sign-up
* normal application traffic

Sign-up flow:

1. User chooses username/email and password.
2. Client creates a certificate request.
3. Server signs the certificate.
4. Client begins authenticated operation.

Certificate lifetimes:

* CA: 20 years
* client certificates: 10 years

The goal is to avoid unnecessary certificate renewal.

---

# Communication Protocol

Transport:

* TLS (preferably mTLS)

Serialization:

* Protocol Buffers

No HTTP.

No gRPC.

The protocol should remain as small as possible.

Only define new messages when required.

---

# Request Model

Every request contains:

* UUID

Every reply contains:

* matching UUID

This allows multiple outstanding asynchronous requests.

Example:

```
Client
   ├── Request A (UUID1)
   ├── Request B (UUID2)
   └── Request C (UUID3)

Server

   Reply UUID2
   Reply UUID1
   Reply UUID3
```

---

## Notifications

The server may send unsolicited notifications.

These have no request UUID.

A separate notification UUID may later be introduced for de-duplication.

Examples:

* feed updated
* article marked read
* feed added
* feed removed

---

# Synchronization

The server owns all persistent state.

Clients keep temporary in-memory copies.

When a client changes something:

1. update local memory immediately
2. send request to server
3. server commits change
4. server broadcasts notification to all connected clients

The originating client does **not** need to wait for the notification before updating its own UI.

---

# Offline Behaviour

Temporary connection failures are tolerated.

If the server is unavailable:

* queue requests in memory

Do **not** persist the queue.

After restart:

* reconnect
* download current state from the server

The server remains the source of truth.

---

# Server Architecture

Three logical layers.

## Business Layer

Handles protocol requests.

Examples:

* list feeds
* retrieve article
* add feed
* mark article read

---

## Data Layer

Database-independent interface.

Business code never issues SQL directly.

Instead it calls operations such as:

* addFeed()
* removeFeed()
* saveArticle()
* listFeeds()

The implementation decides how data is stored.

Possible backends:

* SQLite
* PostgreSQL
* MySQL
* MongoDB
* RocksDB
* others

The business layer must not depend on the storage engine.

Implementation should use pure virtual C++ interfaces.

---

## Database

Initial implementation:

SQLite

Future migration should require only replacing the data layer implementation.

---

# Platform Targets

## Server

Linux only.

Typically deployed:

* Docker container
* local server
* cloud VM

---

## Desktop Client

Supported:

* Linux
* Windows
* macOS

Primary development target:

KDE Plasma

Using KDE integrations where appropriate.

---

# Coding Guidelines

Style:

* GNU formatting

Naming:

Functions:

```
lowerCamelCase()
```

Classes:

```
PascalCase
```

Member variables:

```
member_
```

Lowercase with trailing underscore.

General C++ guidelines:

* C++20
* avoid raw C pointers
* prefer spans/views
* avoid unnecessary complexity
* prioritize readability
* prioritize security

---

# RSS Parsing

Real-world RSS feeds are frequently imperfect.

The parser should therefore be:

* permissive enough to recover useful content from malformed feeds
* strict enough to avoid security issues

Do not reject feeds simply because they are not perfectly compliant.

Recover what can safely be recovered.

---

# Security

Security takes priority over feature count.

Current preview implementation intentionally excludes:

* JavaScript
* CSS execution
* remote resource loading
* automatic image fetching
* embedded active content

Future enhancements may add optional article images, but the initial implementation should display only text.

---

# Design Philosophy

OneRSS should feel like a modern, synchronized version of Akregator rather than an entirely new reading experience.

The implementation should remain intentionally small, adding only the protocol messages, storage operations, and features that are required by the current functionality. Simplicity, correctness, security, and maintainability are preferred over premature generalization or speculative features.
