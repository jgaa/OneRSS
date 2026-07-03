# OneRSS Agent Notes

Keep changes small and implement features end to end.

Relevant guidelines:
- Security first.
- Follow the repo coding style: GNU formatting, C++20, readable code, avoid unnecessary complexity.
- Treat this as a normal desktop app, but keep storage behind a simple server protocol boundary.
- Implement desktop and backend together, feature by feature.
- Desktop code lives in `./desktop/`.
- Backend code lives in `./backend/`.
- Protocol definitions live in `./proto/`.
- Use Akregator as the UX and behavior reference where relevant.
- Local Akregator source is available at `../_others/akregator/`.
