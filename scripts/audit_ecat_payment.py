"""Read invoice balance rows only; no downloads or accounting writes."""
import http.cookiejar
import json
import argparse
import urllib.parse
import urllib.request
from pathlib import Path
from html.parser import HTMLParser
from probe_ecat_pdf import credentials, SafeRedirect, check_url, Tags


class Rows(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.rows, self.row, self.cell = [], None, None
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        if tag == "tr":
            self.row = []
        if tag in ("th", "td") and self.row is not None:
            self.cell = []

    def handle_data(self, data):
        if self.cell is not None:
            self.cell.append(data)

    def handle_endtag(self, tag):
        if tag in ("th", "td") and self.cell is not None:
            self.row.append(" ".join("".join(self.cell).split()))
            self.cell = None
        if tag == "tr" and self.row is not None:
            self.rows.append(self.row)
            self.row = None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("numbers", nargs="+")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    saved = credentials()
    session = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()), SafeRedirect())

    def read(url, fields=None):
        check_url(url)
        payload = None if fields is None else urllib.parse.urlencode(fields).encode()
        with session.open(urllib.request.Request(url, payload), timeout=20) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise RuntimeError("Oversized response")
        return raw.decode("utf-8")

    home = read("https://www.ecat.ua/")
    token = next(a.get("value") for t, a in Tags(home).tags
                 if t == "input" and a.get("name") == "__RequestVerificationToken")
    read("https://www.ecat.ua/account/logonnow", {
        "UserName": saved["login"], "Password": saved["password"], "__RequestVerificationToken": token})
    read("https://www.ecat.ua/elit/ecabinet/index")
    info = json.loads(read("https://www.ecat.ua/Elit/Ecabinet/Url", {}))
    url = info.get("url", "")
    check_url(url)
    if urllib.parse.parse_qs(urllib.parse.urlsplit(url).query).get("login") != [saved["login"]]:
        raise RuntimeError("Unconfirmed cabinet account")
    read(url)
    read("https://ecabinet.ecat.ua:8451/Customer/Balance")
    observed = []
    for number in args.numbers:
        if not number.isalnum():
            raise RuntimeError("Invalid invoice number")
        html = read("https://ecabinet.ecat.ua:8451/Customer/Balance", {
            "Action": "Filter", "ActiveField": "",
            "ActionParam": "DocumentNo=" + number + ";PostingDateFrom=;PostingDateTo=;"})
        rows = Rows(html).rows
        headers = next((r for r in rows if "Документ Но." in r and "Сумма Остатка" in r), None)
        if headers is None:
            raise RuntimeError("Unrecognized balance table")
        names = ["Дата Учета", "Документ Но.", "Сумма", "Сумма Остатка"]
        indices = [headers.index(name) for name in names]
        matching = [r for r in rows if len(r) == len(headers) and r[indices[1]] == number]
        if len(matching) != 1:
            raise RuntimeError("Ambiguous invoice row")
        observation = {"headers": names, "cells": [matching[0][i] for i in indices]}
        observed.append(observation)
        print(number, json.dumps(observation, ensure_ascii=True))
    if args.output:
        args.output.write_text(json.dumps(observed, ensure_ascii=True), encoding="utf-8")


if __name__ == "__main__":
    main()
