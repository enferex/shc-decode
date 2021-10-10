shc-decode: Decode payload data encoded in shc/vaccination QR codes.
====================================================================
shc-decode takes as input the ASCII text that is encoded in some vaccination QR
codes.  This is a form of SMART health representation, see an example of what
that ASCII string looks like [here](https://spec.smarthealth.cards/examples)
and [here](https://spec.smarthealth.cards/examples/example-00-f-qr-code-numeric-value-0.txt).
The output of running shc-decode is the decoded JSON information contained in
that string.

Limitations
-----------
* This tool only decodes strings that have one chunk of data (there is
  only a single slash in the ASCII).
* This tool only decodes the header and payload, not the signature or other
  information following the payload.

Usage
-----
`./shc-decode <ascii-text-from-qr-code.txt>`

Building
--------
Run `make`

Dependencies
------------
* zlib - https://zlib.net/
* lib64 - http://sourceforge.net/projects/libb64

Contact
-------
Matt Davis: https://github.com/enferex
