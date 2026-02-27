from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Network_Async_DB_Report_2026-02-26.md"


def load_source() -> tuple[str, dict[str, str]]:
    text = SOURCE.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    lines = text.split("\n")

    front_lines: list[str] = []
    blocks: dict[str, str] = {}
    current_heading: str | None = None
    current_lines: list[str] = []

    def flush():
        nonlocal current_heading, current_lines
        if current_heading is not None:
            blocks[current_heading] = "\n".join(current_lines).strip() + "\n"

    for line in lines:
        if line.startswith("## "):
            flush()
            current_heading = line
            current_lines = [line]
            continue

        if current_heading is None:
            front_lines.append(line)
        else:
            current_lines.append(line)

    flush()
    front = "\n".join(front_lines).strip() + "\n"
    return front, blocks


def get_block(blocks: dict[str, str], number: str) -> str:
    pattern = re.compile(rf"^## {re.escape(number)}(\s|$)")
    for heading, block in blocks.items():
        if pattern.match(heading):
            return block.strip() + "\n"
    raise KeyError(number)


def write_text(path: Path, parts: list[str]):
    content = "\n\n".join(part.strip("\n") for part in parts if part and part.strip())
    path.write_text(content.strip() + "\n", encoding="utf-8")


def build_team_share(front: str, blocks: dict[str, str]) -> str:
    parts = [
        front,
        "## Diagram Map",
        "| Diagram | File |\n|---|---|\n| Architecture Overview | `assets/01-architecture-overview.svg` |\n| Session UML | `assets/02-session-uml.svg` |\n| Client Lifecycle Sequence | `assets/03-client-lifecycle-sequence.svg` |\n| Async DB Flow Sequence | `assets/04-async-db-flow-sequence.svg` |\n| Graceful Shutdown Sequence | `assets/05-graceful-shutdown-sequence.svg` |\n| DB Reconnect Sequence | `assets/06-db-reconnect-sequence.svg` |",
        get_block(blocks, "1."),
        get_block(blocks, "2."),
        "![Architecture Overview](assets/01-architecture-overview.svg)",
        get_block(blocks, "3.1"),
        get_block(blocks, "3.2"),
        "![Session UML](assets/02-session-uml.svg)",
        get_block(blocks, "3.3"),
        "![Client Lifecycle Sequence](assets/03-client-lifecycle-sequence.svg)",
        get_block(blocks, "3.4"),
        get_block(blocks, "3.5"),
        get_block(blocks, "3.6"),
        get_block(blocks, "4.1"),
        "![Async DB Flow Sequence](assets/04-async-db-flow-sequence.svg)",
        get_block(blocks, "4.2"),
        get_block(blocks, "4.3"),
        get_block(blocks, "4.4"),
        "![DB Reconnect Sequence](assets/06-db-reconnect-sequence.svg)",
        get_block(blocks, "4.5"),
        get_block(blocks, "5.1"),
        get_block(blocks, "5.2"),
        get_block(blocks, "5.3"),
        get_block(blocks, "5.4"),
        get_block(blocks, "6.1"),
        get_block(blocks, "6.2"),
        "![Graceful Shutdown Sequence](assets/05-graceful-shutdown-sequence.svg)",
        get_block(blocks, "7.1"),
        get_block(blocks, "7.2"),
        get_block(blocks, "8.1"),
        get_block(blocks, "8.2"),
        get_block(blocks, "9."),
        get_block(blocks, "10."),
    ]
    return "\n\n".join(parts).strip() + "\n"


def build_executive_summary(front: str, blocks: dict[str, str]) -> str:
    non_empty_lines = [line for line in front.splitlines() if line.strip()]
    title = non_empty_lines[0]
    meta = non_empty_lines[1:5]
    parts = [
        title,
        *meta,
        "## Key Diagrams",
        "![Architecture Overview](assets/01-architecture-overview.svg)",
        "![Async DB Flow Sequence](assets/04-async-db-flow-sequence.svg)",
        "![Graceful Shutdown Sequence](assets/05-graceful-shutdown-sequence.svg)",
        get_block(blocks, "1."),
        get_block(blocks, "6.2"),
        get_block(blocks, "7.1"),
        get_block(blocks, "7.2"),
        get_block(blocks, "8.1"),
        get_block(blocks, "8.2"),
        get_block(blocks, "9."),
    ]
    return "\n\n".join(parts).strip() + "\n"


def build_wiki_home(front: str) -> str:
    summary_lines = [line for line in front.splitlines() if line.strip()][:5]
    parts = [
        "\n".join(summary_lines),
        "## Pages",
        "1. [01-Overall-Architecture.md](01-Overall-Architecture.md)",
        "2. [02-Network-and-Session-Flow.md](02-Network-and-Session-Flow.md)",
        "3. [03-Async-DB-Flow.md](03-Async-DB-Flow.md)",
        "4. [04-Graceful-Shutdown-and-Reconnect.md](04-Graceful-Shutdown-and-Reconnect.md)",
        "5. [05-Operational-Notes.md](05-Operational-Notes.md)",
        "## Diagrams",
        "- `assets/01-architecture-overview.svg`",
        "- `assets/02-session-uml.svg`",
        "- `assets/03-client-lifecycle-sequence.svg`",
        "- `assets/04-async-db-flow-sequence.svg`",
        "- `assets/05-graceful-shutdown-sequence.svg`",
        "- `assets/06-db-reconnect-sequence.svg`",
    ]
    return "\n\n".join(parts).strip() + "\n"


def build_sidebar() -> str:
    return "\n".join(
        [
            "* [Home](Home.md)",
            "* [Overall Architecture](01-Overall-Architecture.md)",
            "* [Network and Session Flow](02-Network-and-Session-Flow.md)",
            "* [Async DB Flow](03-Async-DB-Flow.md)",
            "* [Graceful Shutdown and Reconnect](04-Graceful-Shutdown-and-Reconnect.md)",
            "* [Operational Notes](05-Operational-Notes.md)",
        ]
    ) + "\n"


def build_top_readme() -> str:
    return "\n".join(
        [
            "# Reports",
            "",
            "## Packages",
            "",
            "- [TeamShare](TeamShare/README.md): technical package with detailed code-path notes and inline diagrams.",
            "- [ExecutiveSummary](ExecutiveSummary/README.md): concise package for leads, PMs, and reporting.",
            "- [WikiPackage](WikiPackage/Home.md): split pages ready to adapt into a wiki or internal docs.",
            "",
            "## Assets",
            "",
            "- Each package includes `README/Home + diagrams + assets + DOCX`.",
            "- Mermaid source files are stored in each package `diagrams/` directory.",
            "- Render and DOCX build helpers are under `Doc/Reports/_scripts/`.",
        ]
    ) + "\n"


def main():
    front, blocks = load_source()

    team_dir = ROOT / "TeamShare"
    exec_dir = ROOT / "ExecutiveSummary"
    wiki_dir = ROOT / "WikiPackage"

    write_text(ROOT / "README.md", [build_top_readme()])
    write_text(team_dir / "README.md", [build_team_share(front, blocks)])
    write_text(exec_dir / "README.md", [build_executive_summary(front, blocks)])
    write_text(wiki_dir / "Home.md", [build_wiki_home(front)])

    write_text(
        wiki_dir / "01-Overall-Architecture.md",
        [
            "![Architecture Overview](assets/01-architecture-overview.svg)",
            get_block(blocks, "1."),
            get_block(blocks, "2."),
            get_block(blocks, "3.1"),
            get_block(blocks, "3.2"),
        ],
    )
    write_text(
        wiki_dir / "02-Network-and-Session-Flow.md",
        [
            "![Session UML](assets/02-session-uml.svg)",
            "![Client Lifecycle Sequence](assets/03-client-lifecycle-sequence.svg)",
            get_block(blocks, "3.3"),
            get_block(blocks, "3.4"),
            get_block(blocks, "3.5"),
            get_block(blocks, "3.6"),
        ],
    )
    write_text(
        wiki_dir / "03-Async-DB-Flow.md",
        [
            "![Async DB Flow Sequence](assets/04-async-db-flow-sequence.svg)",
            get_block(blocks, "4.1"),
            get_block(blocks, "4.2"),
            get_block(blocks, "4.3"),
            get_block(blocks, "4.4"),
            get_block(blocks, "5.1"),
            get_block(blocks, "5.2"),
            get_block(blocks, "5.3"),
        ],
    )
    write_text(
        wiki_dir / "04-Graceful-Shutdown-and-Reconnect.md",
        [
            "![DB Reconnect Sequence](assets/06-db-reconnect-sequence.svg)",
            "![Graceful Shutdown Sequence](assets/05-graceful-shutdown-sequence.svg)",
            get_block(blocks, "4.5"),
            get_block(blocks, "6.2"),
            get_block(blocks, "7.1"),
            get_block(blocks, "7.2"),
        ],
    )
    write_text(
        wiki_dir / "05-Operational-Notes.md",
        [
            get_block(blocks, "8.1"),
            get_block(blocks, "8.2"),
            get_block(blocks, "9."),
            get_block(blocks, "10."),
        ],
    )
    write_text(wiki_dir / "_Sidebar.md", [build_sidebar()])


if __name__ == "__main__":
    main()
