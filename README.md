# OneRSS

OneRSS is a self-hosted RSS reader with native desktop and Android clients.

The goal is to build a fast, simple, and secure RSS reader that keeps your feeds and reading state synchronized across all of your devices, without relying on a web application.

The desktop client is built with Qt/QML and is inspired by KDE's Akregator, while using a lightweight backend that exposes only the functionality required by the clients.

## Goals

* Native desktop application
* Native Android application
* Self-hosted backend
* Synchronized feeds and read state
* Simple, secure protocol
* Small and maintainable codebase

## Design

OneRSS uses a client/server architecture.

The server stores all persistent state, while clients keep an in-memory cache for responsiveness. Changes are synchronized between all connected devices in real time.

Communication uses Protocol Buffers over mutually authenticated TLS (mTLS).

The initial server implementation uses SQLite behind a database abstraction layer, making it possible to support other storage engines later without affecting the business logic.

## User Interface

The desktop client follows the familiar three-pane RSS reader layout:

* Feed tree
* Article list
* Article preview

Akregator serves as the reference implementation for the basic user experience, with additional functionality added where it improves usability.

## Security

Security is a primary design goal.

The built-in article preview renders only the article content. JavaScript, remote resources, and other active web content are intentionally disabled to reduce the attack surface.

## Status

Early development.

The project is currently focused on establishing the core architecture, synchronization protocol, and native desktop client.
