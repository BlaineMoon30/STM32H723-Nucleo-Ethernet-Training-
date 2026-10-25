# -*- coding: utf-8 -*-
"""makefsdata (Python) — lwIP httpd용 fsdata_custom.c 생성기.

  py makefsdata.py <fs 폴더> <출력 fsdata_custom.c>

lwIP의 makefsdata(C/Perl)와 같은 구조를 만든다. LWIP_HTTPD_DYNAMIC_HEADERS=1 을 전제로
HTTP 헤더는 넣지 않는다(httpd가 확장자로 Content-Type을 붙임). .shtml/.ssi/.shtm 는 FS_FILE_FLAGS_SSI.

생성 파일은 fs.c 안에서 '#include HTTPD_FSDATA_FILE' 로만 쓰인다. 파일이 include 경로(LWIP/App)에
있어 CubeIDE가 따로 컴파일도 하므로, 단독 컴파일 때는 빈 번역 단위가 되도록 fs.h 가드로 감싼다.
"""
import os, sys

SSI_EXT = (".shtml", ".shtm", ".ssi", ".xml", ".json")


def c_ident(name):
    return "".join(ch if ch.isalnum() else "_" for ch in name)


def hexlines(data, per=16):
    out = []
    for i in range(0, len(data), per):
        out.append("".join(f"0x{b:02x}," for b in data[i:i + per]))
    return "\n".join(out)


def build(src_dir, out_path):
    files = []
    for root, _, names in os.walk(src_dir):
        for n in sorted(names):
            full = os.path.join(root, n)
            url = "/" + os.path.relpath(full, src_dir).replace(os.sep, "/")
            files.append((url, open(full, "rb").read()))
    files.sort(key=lambda f: f[0])
    # index 파일이 목록 맨 앞(FS_ROOT)에 오도록
    files.sort(key=lambda f: 0 if f[0].startswith("/index.") else 1)

    L = ["/* 자동 생성: tools/makefsdata.py — 직접 고치지 말고 fs/ 폴더를 고친 뒤 다시 생성 */",
         "#ifdef LWIP_HDR_APPS_FS_H   /* fs.c 안에서 include 될 때만 내용이 있다 */",
         "",
         "#include \"lwip/apps/fs.h\"",
         "#include \"lwip/def.h\"",
         "",
         "#define file_NULL (struct fsdata_file *) NULL",
         ""]
    for url, data in files:
        ident = c_ident(url)
        name = url.encode("ascii") + b"\0"
        L.append(f"static const unsigned char data_{ident}[] = {{")
        L.append(f"/* {url} ({len(name)} chars) */")
        L.append(hexlines(name))
        L.append(f"/* raw file data ({len(data)} bytes) */")
        L.append(hexlines(data))
        L.append("};")
        L.append("")
    prev = "file_NULL"
    for url, data in reversed(files):
        ident = c_ident(url)
        nlen = len(url) + 1
        flags = "FS_FILE_FLAGS_SSI" if url.lower().endswith(SSI_EXT) else "0"
        L.append(f"const struct fsdata_file file_{ident}[] = {{ {{ {prev}, data_{ident}, data_{ident} + {nlen}, "
                 f"sizeof(data_{ident}) - {nlen}, {flags} }} }};")
        prev = f"file_{ident}"
    L += ["", f"#define FS_ROOT {prev}", f"#define FS_NUMFILES {len(files)}", "",
          "#else", "typedef int fsdata_custom_standalone_unused_t;   /* 단독 컴파일: 빈 번역 단위 방지 */",
          "#endif", ""]
    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(L))
    print(f"{out_path}: {len(files)} files -> " + ", ".join(u for u, _ in files))


if __name__ == "__main__":
    build(sys.argv[1], sys.argv[2])
