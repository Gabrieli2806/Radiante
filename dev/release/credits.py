"""Thanks for a release: who reported, helped with or wrote what went into it, from GitHub.

    python dev/release/credits.py <owner/repo> <version> [--changelog changelog.md] [--plain]

The window is the version's own: from the date of the previous version's heading in changelog.md
("## [1.1.0] - 2026-10-24") to now. In it, it lists:

  - the authors of the issues closed in the window (they reported what was fixed), with the issues;
  - the people who commented on those issues (repro steps, screenshots, testing), with the issues;
  - the authors of the pull requests merged in the window, with the pull requests.

The repository's owner and bots are left out. It prints a Markdown section to append to the release notes, or nothing
when there is no one to thank. --plain writes names without @, for stores where a mention means nothing (Modrinth,
CurseForge). Reads GITHUB_TOKEN from the environment when it is set (the workflow's token is enough: issues and pull
requests are read only).
"""
import argparse
import datetime
import json
import os
import re
import sys
import urllib.parse
import urllib.request

API = "https://api.github.com"


def get(path, params=None):
    """Every page of a GitHub list."""
    url = API + path + ("?" + urllib.parse.urlencode(params) if params else "")
    out = []
    while url:
        request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                                       "User-Agent": "radiante-release-credits"})
        token = os.environ.get("GITHUB_TOKEN")
        if token:
            request.add_header("Authorization", "Bearer " + token)
        with urllib.request.urlopen(request, timeout=30) as response:
            out.extend(json.load(response))
            match = re.search(r'<([^>]+)>;\s*rel="next"', response.headers.get("Link", ""))
            url = match.group(1) if match else None
    return out


def window_start(changelog, version):
    """The date of the heading after this version's in the changelog: the previous release."""
    headings = re.findall(r"^## \[([^\]]+)\](?: - (\d{4}-\d{2}-\d{2}))?", open(changelog, encoding="utf-8").read(),
                          re.MULTILINE)
    names = [name for name, _ in headings]
    if version not in names:
        return None
    for name, date in headings[names.index(version) + 1:]:
        if date:
            return date
    return None


def is_person(user, owner):
    return user and user.get("type") == "User" and user["login"].lower() != owner.lower() \
        and not user["login"].endswith("[bot]")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("repo")
    parser.add_argument("version")
    parser.add_argument("--changelog", default="changelog.md")
    parser.add_argument("--plain", action="store_true")
    args = parser.parse_args()
    owner = args.repo.split("/")[0]
    since = window_start(args.changelog, args.version)
    if since is None:
        return
    since_time = since + "T00:00:00Z"

    reported = {}  # login -> issue numbers
    helped = {}
    coded = {}
    titles = {}
    for item in get(f"/repos/{args.repo}/issues", {"state": "closed", "since": since_time, "per_page": 100}):
        if not item.get("closed_at") or item["closed_at"] < since_time:
            continue
        number = item["number"]
        titles[number] = item["title"]
        if "pull_request" in item:
            pull = get_pull(args.repo, number)
            if pull.get("merged_at") and is_person(item["user"], owner):
                coded.setdefault(item["user"]["login"], []).append(number)
            continue
        if item.get("state_reason") == "not_planned":
            continue
        if is_person(item["user"], owner):
            reported.setdefault(item["user"]["login"], []).append(number)
        if item.get("comments"):
            for comment in get(f"/repos/{args.repo}/issues/{number}/comments", {"per_page": 100}):
                login = comment["user"]["login"] if comment.get("user") else None
                if is_person(comment.get("user"), owner) and number not in reported.get(login, []):
                    helped.setdefault(login, [])
                    if number not in helped[login]:
                        helped[login].append(number)

    people = sorted(set(reported) | set(helped) | set(coded), key=str.lower)
    if not people:
        return
    mention = (lambda login: login) if args.plain else (lambda login: "@" + login)
    refs = lambda numbers: ", ".join(f"#{n}" for n in sorted(numbers))
    lines = ["", "## Thanks", "", "To everyone who reported, tested and wrote what went into this version:", ""]
    for login in people:
        parts = []
        if login in coded:
            parts.append("code in " + refs(coded[login]))
        if login in reported:
            parts.append("reported " + refs(reported[login]))
        if login in helped:
            parts.append("helped on " + refs(helped[login]))
        lines.append(f"- {mention(login)}: " + "; ".join(parts))
    print("\n".join(lines))


def get_pull(repo, number):
    request = urllib.request.Request(f"{API}/repos/{repo}/pulls/{number}",
                                     headers={"Accept": "application/vnd.github+json",
                                              "User-Agent": "radiante-release-credits"})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        request.add_header("Authorization", "Bearer " + token)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:  # Thanks are never worth a failed release.
        print(f"credits: {error}", file=sys.stderr)
