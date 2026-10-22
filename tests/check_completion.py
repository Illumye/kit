#!/usr/bin/env python3
"""
Drives each shell through the completion scripts the examples print, the
way a person would: type a few characters, press TAB, read what the line
became. The unit suite checks what the program answers; this checks that the
four scripts carry the answer to the line intact, which only a real shell
can say.

bash and zsh complete nowhere but in an interactive session, so they run in
a pseudo-terminal. Keystrokes queue up in it while the completion runs, so
nothing here waits on a clock: a marker command typed after the TAB comes
back once the TAB is done. fish and PowerShell complete a line on request,
with complete -C and TabExpansion2. A shell that is not installed is skipped
and said so.

    python3 tests/check_completion.py [shell...]
"""

import base64
import os
import re
import select
import shutil
import subprocess
import sys
import tempfile
import time

ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXAMPLES = os.path.join(ROOT, "examples")
WINDOWS  = os.name == "nt"
TIMEOUT  = 10

# Each program, and how it prints its script for a shell: build has a
# completion command, config an option, since it has no commands.
PROGRAMS = {
    "build":  lambda shell: ["completion", shell],
    "config": lambda shell: ["--completion=" + shell],
}


def program_path(name):
    return os.path.join(EXAMPLES, name + (".exe" if WINDOWS else ""))

# What one TAB turns a line into, where the answer is unique.
INSERTIONS = [
    ("build --col",                    "build --color"),
    ("build --color=al",               "build --color=always"),
    ("build --color al",               "build --color always"),
    ("build ru",                       "build run"),
    ("build com",                      "build completion"),
    ("build -v run --verb",            "build -v run --verbose"),
    ("build completion z",             "build completion zsh"),
    ("config examples/demo/app.conf server.p", "config examples/demo/app.conf server.port"),
]

# The same, where the shell's own file completion takes over. Only a line
# can be compared: how a shell spells the path it offers is its own affair.
FILES = [
    ("build run -- examples/demo/app", "build run -- examples/demo/app.conf"),
]

# What a line offers, where there is a choice to make. The empty word after
# a space is the case PowerShell before 7.3 gets wrong unless told.
OFFERS = [
    ("build ",                         ["run", "clean", "completion"]),
    ("build --color=",                 ["auto", "always", "never"]),
    ("build -",                        ["--verbose", "--color", "--help"]),
    ("build completion ",              ["bash", "zsh", "fish", "powershell"]),
    ("config examples/demo/app.conf limits.",
     ["limits.max_connections", "limits.rate_per_second"]),
]


def environment(home):
    env = dict(os.environ)
    env["PATH"] = EXAMPLES + os.pathsep + env.get("PATH", "")
    env["HOME"] = home
    env["PS1"] = "$ "
    env["TERM"] = "dumb"
    env["HISTFILE"] = os.devnull
    return env


class Terminal:
    """An interactive shell on the far side of a pseudo-terminal."""

    def __init__(self, argv, env, setup):
        import pty
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(ROOT)
            os.execvpe(argv[0], argv, env)
        self.marks = 0
        self.run(setup)

    def read_until(self, pattern):
        text, end = "", time.time() + TIMEOUT
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.1)
            if ready:
                try:
                    text += os.read(self.fd, 65536).decode(errors="replace").replace("\r", "")
                except OSError:
                    break
                match = re.search(pattern, text, re.M)
                if match:
                    return text, match
        return text, None

    def run(self, command):
        """Runs a command and waits until the shell is done with it."""
        self.marks += 1
        mark = f"READY{self.marks}"
        os.write(self.fd, f"{command}; echo {mark}\n".encode())
        self.read_until(rf"^{mark}$")

    def line_after(self, typed, tabs):
        """Types, presses TAB, then prefixes the line with an echo and runs it:
        what it prints is the line as the completion left it, and everything
        before it is what the TAB displayed."""
        self.marks += 1
        mark = f"LINE{self.marks}:"
        os.write(self.fd, (typed + "\t" * tabs + f"\x01echo {mark} \n").encode())
        text, match = self.read_until(rf"^{mark} (.*)$")
        if not match:
            return None, text
        return match.group(1).rstrip(), text[:match.start()]

    def close(self):
        os.write(self.fd, b"exit\n")
        os.waitpid(self.pid, 0)


def loading(shell):
    """The lines that load every program's script, in the shell's syntax."""
    for name, arguments in PROGRAMS.items():
        command = " ".join([name] + arguments(shell))
        if shell in ("bash", "zsh"):
            yield f'eval "$({command})"'
        elif shell == "fish":
            yield f"{command} | source"
        else:
            yield f"& {command} | Out-String | Invoke-Expression"


def interactive(shell, env, home):
    if shell == "bash":
        return Terminal(["bash", "--norc", "--noprofile", "-i"], env,
                        "; ".join(loading("bash")))
    return Terminal(["zsh", "-f", "-i"], env,
                    f"bindkey -e; autoload -Uz compinit; compinit -u -d {home}/zcompdump; "
                    + "; ".join(loading("zsh")))


def check_interactive(shell, env, home):
    term, failures = interactive(shell, env, home), []
    for typed, wanted in INSERTIONS + FILES:
        got, _ = term.line_after(typed, 1)
        if got != wanted:
            failures.append(f"{typed!r} became {got!r}, not {wanted!r}")
    for typed, offered in OFFERS:
        _, shown = term.line_after(typed, 2)
        missing = [word for word in offered if word not in shown]
        if missing:
            failures.append(f"{typed!r} did not show {missing}")
    term.close()
    return failures


def candidates_fish(env):
    lines = "\n".join(typed for typed, _ in INSERTIONS + FILES + OFFERS)
    script = ("\n".join(loading("fish")) + "\n"
              "for line in (string split \\n -- $KIT_LINES)\n"
              "    printf 'LINE\\t%s\\n' $line\n"
              "    complete -C $line\n"
              "end\n")
    env = dict(env, KIT_LINES=lines)
    out = subprocess.run(["fish", "--no-config", "-c", script], env=env, cwd=ROOT,
                         capture_output=True, text=True, timeout=60)
    return parse_candidates(out.stdout)


def candidates_powershell(executable, env):
    lines = "\n".join(typed for typed, _ in INSERTIONS + FILES + OFFERS)
    script = ("\n".join(loading("powershell")) + "\n"
              "foreach ($line in ($env:KIT_LINES -split \"`n\")) {\n"
              "    \"LINE`t$line\"\n"
              "    $r = TabExpansion2 -inputScript $line -cursorColumn $line.Length\n"
              "    foreach ($m in $r.CompletionMatches) { $m.CompletionText }\n"
              "}\n")
    env = dict(env, KIT_LINES=lines)
    # Encoded, because a script of several lines does not survive the trip
    # through a Windows command line as a plain argument.
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    out = subprocess.run([executable, "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                         env=env, cwd=ROOT, capture_output=True, text=True, timeout=120)
    return parse_candidates(out.stdout)


def parse_candidates(output):
    """LINE<tab>typed, then one candidate per line, its description cut off."""
    found, current = {}, None
    for line in output.splitlines():
        if line.startswith("LINE\t"):
            current = line[5:]
            found.setdefault(current, [])
        elif current is not None:
            found[current].append(line.split("\t")[0])
    return found


def check_candidates(found):
    failures = []
    for typed, wanted in FILES:
        got = found.get(typed) or []
        name = os.path.basename(wanted)
        if not any(candidate.rstrip("'\"").endswith(name) for candidate in got):
            failures.append(f"{typed!r} offered {got!r}, without {name!r}")
    for typed, wanted in INSERTIONS:
        got = found.get(typed)
        # The candidate replaces the word under the cursor, so the line it
        # leads to is the line minus that word, plus the candidate.
        head = typed[:len(typed) - len(typed.split(" ")[-1])]
        lines = [head + candidate for candidate in (got or [])]
        if wanted not in lines:
            failures.append(f"{typed!r} offered {got!r}, which does not give {wanted!r}")
    for typed, offered in OFFERS:
        got = found.get(typed) or []
        missing = [word for word in offered if not any(word in candidate for candidate in got)]
        if missing:
            failures.append(f"{typed!r} offered {got!r}, without {missing}")
    return failures


def main():
    wanted = sys.argv[1:] or ["bash", "zsh", "fish", "powershell", "pwsh"]
    for name in PROGRAMS:
        if not os.path.exists(program_path(name)):
            print(f"completion: {program_path(name)} is not built")
            return 1

    status = 0
    with tempfile.TemporaryDirectory() as home:
        env = environment(home)
        for shell in wanted:
            if not shutil.which(shell):
                print(f"completion: {shell} skipped, not installed")
                continue
            if shell in ("bash", "zsh"):
                if WINDOWS:
                    print(f"completion: {shell} skipped, no pseudo-terminal on Windows")
                    continue
                failures = check_interactive(shell, env, home)
            elif shell == "fish":
                failures = check_candidates(candidates_fish(env))
            else:
                failures = check_candidates(candidates_powershell(shell, env))

            if failures:
                status = 1
                print(f"completion: {shell} FAILED")
                for failure in failures:
                    print(f"  {failure}")
            else:
                print(f"completion: {shell} ok ({len(INSERTIONS) + len(FILES) + len(OFFERS)} lines)")
    return status


if __name__ == "__main__":
    sys.exit(main())
