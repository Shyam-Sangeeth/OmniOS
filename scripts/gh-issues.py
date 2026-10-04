# GitHub issues for this repository, with no gh CLI: the token comes from
# Git Credential Manager (the one `git push` uses) and is never printed.
#
#   python scripts/gh-issues.py list
#   python scripts/gh-issues.py open issue.json     {"title": ..., "body": ..., "labels": [...]}
#   python scripts/gh-issues.py close NUMBER "comment"
import json
import subprocess
import sys
import urllib.error
import urllib.request

REPO = 'Shyam-Sangeeth/OmniOS'


def token():
    out = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n\n',
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.startswith('password='):
            return line[len('password='):]
    raise SystemExit('no github.com credential in Git Credential Manager')


def call(method, path, body=None):
    req = urllib.request.Request(
        f'https://api.github.com/repos/{REPO}/{path}', method=method,
        data=json.dumps(body).encode('utf-8') if body is not None else None,
        headers={'Authorization': 'Bearer ' + token(), 'Accept': 'application/vnd.github+json',
                 'X-GitHub-Api-Version': '2022-11-28', 'User-Agent': 'omnios-issue-script'})
    try:
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        raise SystemExit(f'HTTP {e.code}: ' + e.read().decode('utf-8', 'replace')[:300])


def main(args):
    if args[:1] == ['list']:
        for issue in call('GET', 'issues?state=all&per_page=100'):
            if 'pull_request' not in issue:
                print(f"#{issue['number']} [{issue['state']}] {issue['title']} ({issue['comments']} comments)")
    elif args[:1] == ['open'] and len(args) == 2:
        made = call('POST', 'issues', json.load(open(args[1], encoding='utf-8')))
        print(made['number'], made['html_url'])
    elif args[:1] == ['close'] and len(args) == 3:
        number, comment = args[1], args[2]
        issue = call('GET', f'issues/{number}')
        call('POST', f'issues/{number}/comments', {'body': comment})
        if issue['state'] == 'open':
            issue = call('PATCH', f'issues/{number}', {'state': 'closed', 'state_reason': 'completed'})
        print(issue['state'], issue['html_url'])
    else:
        raise SystemExit(__doc__ or 'usage: gh-issues.py list | open FILE.json | close NUMBER "comment"')


if __name__ == '__main__':
    main(sys.argv[1:])
