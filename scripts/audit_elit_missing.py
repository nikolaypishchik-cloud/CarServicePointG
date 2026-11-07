"""Compare saved observations with read-only SOAP lists; never print credentials."""
import argparse
import json
import sqlite3
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path
from probe_ecat_pdf import credentials


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise RuntimeError("SOAP redirect rejected")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("copy", type=Path)
    args = parser.parse_args()
    auth = credentials()
    opener = urllib.request.build_opener(NoRedirect)

    def request(method, page=None):
        envelope = ET.Element("{http://schemas.xmlsoap.org/soap/envelope/}Envelope")
        body = ET.SubElement(envelope, "{http://schemas.xmlsoap.org/soap/envelope/}Body")
        call = ET.SubElement(body, "{http://buyer.elit.cz/}" + method)
        fields = {k: auth[k] for k in ("company", "login", "password")}
        fields.update(open="true", closed="true", overdue="true")
        if page is not None:
            fields.update(pageNo=str(page), pageSize="100")
        for key, value in fields.items():
            ET.SubElement(call, key).text = value
        req = urllib.request.Request(
            "https://econnector.elit.cz/InterCompany-1.75.0/BuyerService",
            ET.tostring(envelope), {"Content-Type": "text/xml; charset=utf-8", "SOAPAction": '""'})
        with opener.open(req, timeout=20) as response:
            raw = response.read(4 * 1024 * 1024 + 1)
        if len(raw) > 4 * 1024 * 1024:
            raise RuntimeError("Oversized response")
        return ET.fromstring(raw)

    count = int(request("getNumOfInvoices").find(".//return").text)
    if not 0 <= count <= 10000:
        raise RuntimeError("Invalid count")
    remote = []
    for page in range(1, (count + 99) // 100 + 1):
        remote.extend({child.tag: child.text or "" for child in row}
                      for row in request("getInvoiceList", page).findall(".//return"))
    if len(remote) != count or int(request("getNumOfInvoices").find(".//return").text) != count:
        raise RuntimeError("Remote list changed")
    by_number = {row["invoiceNo"].strip(): row for row in remote}
    print("REMOTE_HEADERS", len(remote))
    for path in args.copy.glob("*.db"):
        with sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True) as db:
            if not db.execute("SELECT 1 FROM sqlite_master WHERE name='elit_invoice_snapshots'").fetchone():
                continue
            for fetched, payload in db.execute("SELECT fetched_at,payload FROM elit_invoice_snapshots"):
                saved = json.loads(payload)
                print("SAVED", path.name, fetched, len(saved))
                for doc in saved:
                    row = by_number.get(doc["number"])
                    if row is None:
                        print("MISSING", doc["number"], doc["posted"], doc["currency"], doc["remaining"])
                    elif row["postingDate"][:10] != doc["posted"] or row["currency"].upper() != doc["currency"]:
                        print("IDENTITY_CHANGED", doc["number"], doc["posted"], row["postingDate"], doc["currency"], row["currency"])


if __name__ == "__main__":
    main()
