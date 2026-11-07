"""eCat export probe; preparation requires --prepare-date. No accounting writes.
Never prints credentials, cookies or authenticated session URLs.
"""
import base64
import ctypes
import json
import re
import urllib.parse
import urllib.request
import http.cookiejar
import winreg
import argparse
import datetime
import time
import io
import zipfile
from html import unescape
from html.parser import HTMLParser


class Tags(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.tags = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        self.tags.append((tag, dict(attrs)))


def credentials():
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                       r"Software\CarServicePointG\CarServicePointG\integrations\elit") as key:
        value = winreg.QueryValueEx(key, "credentials_dpapi")[0]
    if not value.startswith("dpapi:v1:"):
        raise RuntimeError("Unsupported credential storage")

    class Blob(ctypes.Structure):
        _fields_ = [("size", ctypes.c_ulong), ("data", ctypes.POINTER(ctypes.c_ubyte))]

    raw = base64.b64decode(value[9:])
    buf = (ctypes.c_ubyte * len(raw)).from_buffer_copy(raw)
    source, target = Blob(len(raw), buf), Blob()
    if not ctypes.windll.crypt32.CryptUnprotectData(ctypes.byref(source), None, None, None,
                                                 None, 1, ctypes.byref(target)):
        raise RuntimeError("Cannot decrypt saved credentials")
    try:
        return json.loads(ctypes.string_at(target.data, target.size).decode("utf-8"))
    finally:
        ctypes.memset(target.data, 0, target.size)
        ctypes.windll.kernel32.LocalFree(target.data)


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        check_url(newurl)
        if req.data and urllib.parse.urlsplit(newurl).netloc != urllib.parse.urlsplit(req.full_url).netloc:
            raise RuntimeError("Cross-origin POST redirect rejected")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def check_url(url):
    p = urllib.parse.urlsplit(url)
    if p.scheme != "https" or p.netloc not in ("www.ecat.ua", "ecat.ua", "ecabinet.ecat.ua:8451"):
        raise RuntimeError("Unexpected eCat destination")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare-date', type=datetime.date.fromisoformat)
    parser.add_argument('--inspect-export', action='store_true')
    args = parser.parse_args()
    saved = credentials()
    if saved.get("company") != "ELIT_UA":
        raise RuntimeError("This probe supports ELIT_UA only")
    session = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()), SafeRedirect())

    def read(url, fields=None):
        check_url(url)
        payload = None if fields is None else urllib.parse.urlencode(fields).encode()
        request = urllib.request.Request(url, payload, {"User-Agent": "CarServicePointG"})
        with session.open(request, timeout=20) as response:
            body = response.read(4 * 1024 * 1024 + 1)
            if len(body) > 4 * 1024 * 1024:
                raise RuntimeError("Response too large")
            return body.decode("utf-8"), response.url

    home, _ = read("https://www.ecat.ua/")
    tags = Tags(home).tags
    token = next(a.get("value") for t, a in tags if t == "input" and a.get("name") == "__RequestVerificationToken")
    result, final = read("https://www.ecat.ua/account/logonnow", {
        "UserName": saved["login"], "Password": saved["password"], "__RequestVerificationToken": token})
    print("Login response:", urllib.parse.urlsplit(final).path)
    page, _ = read("https://www.ecat.ua/elit/ecabinet/index")
    tags = Tags(page).tags
    frames = [a.get("src", "") for t, a in tags if t == "iframe" and a.get("id") == "ECabinetFrame"]
    if frames and not any(frames):
        session_info, _ = read("https://www.ecat.ua/Elit/Ecabinet/Url", {})
        info = json.loads(session_info)
        print("Session response fields:", list(info))
        frames = [info.get("url", "")]
    print("Cabinet frames:", len(frames))
    for t, a in tags:
        if a.get("id") == "ECabinetFrame":
            print("Frame attribute names:", list(a))
            print("Frame binding:", a.get("ng-src", "").split("?")[0])
            for key, val in a.items():
                if "ecabinet" in val.lower():
                    print("Frame route:", key, urllib.parse.urlsplit(val).path)
    print("Controllers:", [a["ng-controller"] for _, a in tags if "ng-controller" in a])
    for frame in frames:
        if not frame:
            continue
        page, _ = read(urllib.parse.urljoin("https://www.ecat.ua/", frame))
        balance, _ = read("https://ecabinet.ecat.ua:8451/Customer/Balance")
        tags = Tags(balance).tags
        actions = sorted({a["data-action"] for _, a in tags if "data-action" in a})
        print("Balance export actions:", [x for x in actions if "Print" in x or "Download" in x])
        print("Downloadable invoices:", sum(a.get("data-action") == "DownloadInvoice" for _, a in tags))
        print("Print form controls:", [{k:v for k,v in a.items() if k in ('id','name','value','type','class','onclick')} for t,a in tags if t == 'input' and any(s in a.get('name','') for s in ('date-', 'download-type', 'salesdocs-type', 'printing'))])
        filtered, _ = read("https://ecabinet.ecat.ua:8451/Customer/Balance", {
            "Action":"Filter", "ActionParam":"DocumentNo=KAM26F058793;PostingDateFrom=;PostingDateTo=;", "ActiveField":""})
        print("Filtered download links:", sum(a.get("data-action") == "DownloadInvoice" for _, a in Tags(filtered).tags))
        print("Filter controls:", [(t,a.get("name"),a.get("class")) for t,a in tags if 'Filter' in a.get('class','')])
        print("Row attributes:", [a for t,a in tags if t=='tr'][:4])
        print("Export attributes:", [{k:v for k,v in a.items() if k in ('id','data-action','data-action-param','onclick')} for t,a in tags if 'Download' in a.get('onclick','')])
        queue, _ = read("https://ecabinet.ecat.ua:8451/Customer/DownloadDocuments", {
            "filter": "SpecialPrintingType=2;eCatLogin=" + saved["login"]})
        print("Queue controls:", [{k:v for k,v in a.items() if k in ('id','data-action','data-action-param','onclick','name','value')} for t,a in Tags(queue).tags if t in ('button','a','input')])
        if args.inspect_export:
            print('Queue columns:', [unescape(re.sub('<[^>]+>', '', x)).strip() for x in re.findall(r'<t[dh]\b[^>]*>(.*?)</t[dh]>', queue, re.S)])
            download = next((a for t,a in reversed(Tags(queue).tags) if a.get('data-action')=='DownloadInvoice'), None)
            if download:
                payload=urllib.parse.urlencode({'Action':'DownloadInvoice','ActionParam':download['data-action-param']+';','ActiveField':''}).encode()
                with session.open(urllib.request.Request('https://ecabinet.ecat.ua:8451/Customer/Balance',payload),timeout=20) as response:
                    body=response.read(16*1024*1024+1)
                    print('Export format:', body[:4].hex(), 'bytes:',len(body), 'mime:',response.headers.get('Content-Type'), 'disposition:',response.headers.get('Content-Disposition'))
                    if body.startswith(b'PK'):
                        with zipfile.ZipFile(io.BytesIO(body)) as archive:
                            print('PDF members:', [(x.filename,x.file_size,x.compress_size) for x in archive.infolist()])
        if args.prepare_date:
            day = args.prepare_date.isoformat()
            result, _ = read('https://ecabinet.ecat.ua:8451/Customer/Balance', {
                'action': 'AddPrintJob',
                'actionParam': 'DateFrom='+day+';DateTo='+day+';DownloadType=pdf;PrintingDocNotReceived=false;PrintingOnlyInvoices=true;Login='+saved['login'],
                'returnEmpty': 'true'})
            print('Print job accepted:', not result.strip())
            for attempt in range(20):
                queue, _ = read('https://ecabinet.ecat.ua:8451/Customer/DownloadDocuments', {
                    'filter': 'SpecialPrintingType=2;eCatLogin='+saved['login']})
                controls = [{k:v for k,v in a.items() if k in ('id','data-action','data-action-param','onclick','name','value','href')} for t,a in Tags(queue).tags if t in ('button','a','input')]
                print('Print poll', attempt, controls)
                if any(a.get('id')=='print-waiting' and a.get('value')=='0' for _,a in Tags(queue).tags):
                    break
                time.sleep(3)
    print("Cabinet script paths:", [a.get("src", "").split("?")[0] for t, a in tags if t == "script" and a.get("src")])
    if not any(frames):
        for script in re.findall(r"<script[^>]*>(.*?)</script>", page, re.S | re.I):
            if "ECabinetFrame" in script:
                # URL parameters may be session secrets, so expose only literal paths.
                print("Frame setup paths:", re.findall(r'[\"\'](/[^\"\'?]+)[\"\']', script))
                print("Frame setup calls:", re.findall(r'(\w+)\s*\(', script))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Exceptions can contain session URLs; print only the type, never the value.
        print("Probe failed:", type(error).__name__)
        raise SystemExit(1)
