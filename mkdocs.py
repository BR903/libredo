#!/usr/bin/env python3

import argparse
import re
import sys

versionstring = 'mkdocs.py: version 0.9\n'
usagestring = '''\
Usage: mkdocs.py [OPTIONS] [INFILE]
Generate documentation files for libredo.

  -b, --base=BASE       Use BASE as the base output filename
      --man=OUTFILE     Output a section 3 man page
      --html=OUTFILE    Output an HTML document
      --text=OUTFILE    Output a plain text file
      --all             Output in all available formats
      --help            Display this help and exit
      --version         Display version information and exit

If INFILE is "-" or omitted, read from standard input.
'''

class man:
    header = ('.TH {name} 3 "{date}" "{title}"\n'
              '.LO 1\n'
              '.SH NAME\n'
              '{name} \\- {desc}\n'
              '.P\n')

    def settopline(m):
        topline = m.group(1).rstrip()
        man.name, man.title, man.date, man.desc = topline.split(' / ')
        return ''

    def setsynopsis(m):
        man.synopsis = m.group(1)
        return '\n\abr\n\aSH SYNOPSIS' + man.synopsis + '\n'

    def mksection(m):
        title = m.group(1)
        return '\n\abr\n\aSH ' + title.upper()

    def mklist(m):
        items = m.group(1)
        items = items.replace('\n. ', '\n\aTP\n*\n')
        return '\n\aTP 2\n\aPD 0{}\n\aPD 1\n\n'.format(items[4:])

    def mktable(m):
        items = m.group(1)
        items = re.sub(r'\A\n?\. ', '', items)
        cells = items.split('\n. ')
        table = ''
        for n in range(0, len(cells), 2):
            if cells[n].startswith('%'):
                cells[n] = '\aI ' + cells[n].strip('%')
            elif cells[n].endswith('%'):
                prefix, name, _ = cells[n].split('%')
                cells[n] = '\aBI "{}" {}'.format(prefix, name)
            else:
                cells[n] = '\aB ' + cells[n]
            table += '\aTP\n{}\n{}\n'.format(cells[n], cells[n + 1])

        table = re.sub(r'\A\n\aTP', r'\n', table)
        table = re.sub(r'\n*\Z', r'\n\n\n', table)
        return table

    def mkgrid(m):
        head = m.group(1)
        body = m.group(2)
        cols = []
        prev = 0
        if head.startswith(' '):
            h = head.lstrip()
            prev = len(head) - len(h)
            head = h
        for col in re.findall(r'(\S)(\s*)', head):
            n = len(col[1]) + 1
            if n <= 1:
                n = 79
            cols.append([ prev, n, col[0] ])
            prev += n
        rows = []
        for line in body.split('\n'):
            row = []
            for col in cols:
                entry = line[col[0]:col[1]]
                row.append(entry.strip())
            rows.append(row)
        for x in range(0, len(cols)):
            entries = []
            colsize = 0
            for y in range(0, len(rows)):
                text = rows[y][x]
                size = len(re.sub(r'[!#%\`]', '', text))
                entries.append([ text, size, y ])
                if colsize < size:
                    colsize = size
            for entry in entries:
                text, size, y = entry
                pad = colsize - size
                if pad <= 0:
                    continue
                align = cols[x][2]
                if align == 'l':
                    text += '~' * pad
                elif align == 'r':
                    text = '~' * pad + text
                elif align == 'c':
                    before = int(pad / 2)
                    after = pad - before
                    text = '~' * before + text + '~' * after
                rows[y][x] = text
        table = '\n'
        for row in rows:
            table += ''.join(row)
            table = re.sub(r'~*\Z', r'\n\abr\n', table)
        table = re.sub(r'\abr\n\Z', r'\n', table)
        return table

    def generate(text):
        text = re.sub(r'\A\s*(\S[^\n]*)\n', man.settopline, text)
        text = text.replace(man.name, '!' + man.name + '!')

        text = text.replace('\n.indent\n', '\n')
        text = text.replace('\n.formatted\n', '\n')
        text = text.replace('\n.brk\n', '\n\abr\n')

        text = re.sub(r'\n\.list((?:\n[^\n]+)+)\n\n', man.mklist, text)
        text = re.sub(r'\n\.table((?:\n[^\n]+)+)\n\n', man.mktable, text)

        text = re.sub(r'\n+\.Synopsis((?:\n[^\n]+)+)\n\n',
                      man.setsynopsis, text)

        text = re.sub(r'\n+\.section\s+([^\n]+)\n', man.mksection, text)
        text = re.sub(r'\n+\.subsection\s+([^\n]+)\n',
                      r'\n\aP\n\aB "\1"\n', text)

        text = re.sub(r'\n\.grid\n([^\n]+)\n((?:[^\n]+\n)+)\n',
                      man.mkgrid, text)

        text = text.replace('#', '')
        text = text.replace('`', '')
        text = re.sub(r'%([^%]+)%', r'\\fI\1\\fR', text)
        text = re.sub(r'!([^!]+)!', r'\\fB\1\\fR', text)

        text = text.strip() + '\n'
        text = re.sub(r'\n\n+', '\n\aP\n', text)

        text = text.replace('\n.', '\n .')
        text = text.replace('~', '\\ ')
        text = text.replace('\a', '.')

        top = man.header.format(name=man.name, title=man.title,
                                date=man.date, desc=man.desc)
        return top + text

#
#
#

class text:
    toc = []

    def settitle(m):
        text.title = m.group(1).upper()
        return '\n'

    def mkitem(m):
        item = m.group(1).rstrip()
        n = len(text.toc) + 1
        item = re.sub(r'\s+', r' ', item)
        text.toc.append('{}. {}\n'.format(n, item))
        return '\n~\n {}. {}\n'.format(n, item.upper())

    def mkindent(m):
        text = m.group(1)
        text = text.replace('\n', '\n    ')
        return '\n' + text + '\n'

    def mklist(m):
        items = m.group(1)
        items = re.sub(r'\n([^.])', r'\n  \1', items)
        items = re.sub(r'\n\.\s+', r'\n* ', items)
        return '\n' + items + '\n\n'

    def mktable(m):
        lines = m.group(1).split('\n')
        while lines and lines[0] == '':
            lines.pop(0)
        n = 1
        table = ''
        for line in lines:
            if line.startswith('. '):
                n ^= 1
                if n:
                    line = line.replace('.', ' ', 1)
                else:
                    line = line.replace('.', '\n-', 1)
            elif n:
                line = '  ' + line
            table += '\n' + line
        return table + '\n\n'

    def mkgrid(m):
        head = m.group(1)
        body = m.group(2)
        cols = []
        prev = 0
        if head.startswith(' '):
            h = head.lstrip()
            prev = len(head) - len(h)
            head = h
        for col in re.findall(r'(\S)(\s*)', head):
            n = len(col[1]) + 1
            if n <= 1:
                n = 79
            cols.append([ prev, n, col[0] ])
            prev += n
        rows = []
        for line in body.split('\n'):
            row = []
            for col in cols:
                entry = line[col[0]:col[1]]
                row.append(entry.strip())
            rows.append(row)
        for x in range(0, len(cols)):
            entries = []
            colsize = 0
            for y in range(0, len(rows)):
                text = rows[y][x]
                size = len(re.sub(r'[!#%\`]', '', text))
                entries.append([ text, size, y ])
                if colsize < size:
                    colsize = size
            for entry in entries:
                text, size, y = entry
                pad = colsize - size
                if pad <= 0:
                    continue
                align = cols[x][2]
                if align == 'l':
                    text += '~' * pad
                elif align == 'r':
                    text = '~' * pad + text
                elif align == 'c':
                    before = int(pad / 2)
                    after = pad - before
                    text = '~' * before + text + '~' * after
                rows[y][x] = text
        table = ''
        for row in rows:
            table += ''.join(row)
            table = re.sub(r'~*\Z', r'\n', table)
        return '\n' + table + '\n'

    def generate(input):
        input = re.sub(r'\A[^/]+/\s+([^/]*\S)\s+/[^\n]+\n+',
                       text.settitle, input)

        input = input.replace('\n.Synopsis\n', '\n')
        input = re.sub(r'\n\.section\s+([^\n]+)\n', text.mkitem, input)
        input = re.sub(r'\n\.subsection\s+([^\n]+)\n', r'\n  \1\n', input)

        input = re.sub(r'\n\.formatted((?:\n[^\n]+)+)\n\n', r'\1\n\n', input)
        input = re.sub(r'\n\.indent((?:\n[^\n]+)+)\n\n', text.mkindent, input)
        input = re.sub(r'\n\.list((?:\n[^\n]+)+)\n\n', text.mklist, input)
        input = re.sub(r'\n\.table((?:\n[^\n]+)+)\n\n', text.mktable, input)
        input = re.sub(r'\n\.grid\n([^\n]+)\n((?:[^\n]+\n)+)\n',
                       text.mkgrid, input)

        input = re.sub(r'[#%!\`]', '', input)
        input = input.replace('~', ' ')
        input = input.replace('\n.brk\n', '\n')
        input = re.sub(r'\n\n\n+', '\n\n', input)

        contents = ''
        if len(text.toc) > 2:
            contents = 'Contents\n\n' + ''.join(text.toc)
        return text.title + '\n\n' + contents + input

#
#
#

class html:
    header = ('<!DOCTYPE html>\n'
              '<html>\n'
              '<head>\n'
              '<title>{title}</title>\n'
              '<style>\n{style}\n</style>\n'
              '</head>\n'
              '<body>\n'
              '<h1>{title}</h1>\n')
    bottom = '</body>\n</html>\n'
    stylesheet = ('body { margin: 1em; max-width: 72em; }\n'
                  'h1 { text-align: center; padding-bottom: 1em; }\n'
                  '.table td { vertical-align: top; padding: 0.5em 1em; }\n'
                  '.grid td { padding: 0; }')
    toc = []
    tocmap = {}

    def settitle(m):
        html.title = m.group(1).rstrip()
        return '\n'

    def setsynopsis(m):
        html.synopsis = '<p>{}\n'.format(m.group(1).rstrip())
        return '\n'

    def additem(m):
        item = re.sub(r'\s+', ' ', m.group(1).rstrip())
        fragment = len(html.toc)
        html.toc.append(item)
        html.tocmap[item] = fragment
        return '\n\n<a name="{}"></a>\n<h2>{}</h2>\n\n'.format(fragment, item)

    def lookupitem(m):
        item = re.sub(r'\s+', ' ', m.group(1).rstrip())
        if not item in html.tocmap:
            sys.stderr.write('warning: invalid section "{}"\n'.format(item))
            html.tocmap[item] = '__' + item
        return '<a href="#{}">{}</a>'.format(html.tocmap(item), item)

    def mklist(m):
        items = m.group(1)
        items = re.sub(r'\n\.\s+', r'\n<li>', items)
        return '\n<ul>{}\n</ul>\n\n'.format(items)

    def mktable(m):
        items = m.group(1)
        table = ''
        n = 0
        while True:
            pos = items.find('\n. ')
            if pos < 0:
                break
            table += items[0:pos]
            items = items[pos+3:]
            n ^= 1
            if n:
                table += '</td></tr>\n<tr><td>'
            else:
                table += '</td>\n<td>'
        table += items + '</td></tr>\n</table>\n\n'
        return re.sub(r'\A</td></tr>', r'\n<table class="table">', table)

    def mkgrid(m):
        head = m.group(1)
        body = m.group(2).rstrip()
        cols = []
        align = { 'l': 'left', 'r': 'right', 'c': 'center' }
        prev = 0
        if head.startswith(' '):
            h = head.lstrip()
            prev = len(head) - len(h)
            head = h
        for col in re.findall(r'(\S)(\s*)', head):
            n = len(col[1]) + 1
            if n <= 1:
                n = 79
            cols.append([ prev, n, align[col[0]] ])
            prev += n
        table = ''
        for line in body.split('\n'):
            table += '<tr>\n<td>~</td>\n'
            for col in cols:
                entry = line[col[0]:col[1]].strip()
                table += '<td align="{}">{}</td>\n'.format(col[2], entry)
            table += '</tr>\n'
        return '\n<table class="grid">\n{}</table>\n<p>\n'.format(table)

    def generate(text):
        text = re.sub(r'\A[^/]+/\s+([^/]*\S)\s+/[^\n]+\n+',
                      html.settitle, text)
        text = re.sub(r'\n\.Synopsis((?:\n[^\n]+)+)\n',
                      html.setsynopsis, text)

        text = re.sub(r'\n\.section\s+([^\n]+)\n', html.additem, text)
        text = re.sub(r'\n\.subsection\s+([^\n]+)\n', r'\n<h3>\1</h3>\n', text)
        text = re.sub(r'\n\.indent((?:\n[^\n]+)+)\n\n',
                      r'\n<blockquote>\1</blockquote>\n\n', text)
        text = re.sub(r'\n\.formatted((?:\n[^\n]+)+)\n\n',
                      r'\n<pre>\1</pre>\n\n', text)
        text = re.sub(r'\n\.list((?:\n[^\n]+)+)\n\n', html.mklist, text)
        text = re.sub(r'\n\.table((?:\n[^\n]+)+)\n\n', html.mktable, text)
        text = re.sub(r'\n\.grid\n([^\n]+)\n((?:[^\n]+\n)+)\n',
                      html.mkgrid, text)

        text = re.sub(r'#([^#]+)#', html.lookupitem, text)
        text = re.sub(r'`([^`]+)`', r'<tt>\1</tt>', text)
        text = re.sub(r'%([^%]+)%', r'<i>\1</i>', text)
        text = re.sub(r'!([^!]+)!', r'<b>\1</b>', text)

        text = re.sub(r'&', r'&amp;', text)
        text = re.sub(r'~', r'&nbsp;', text)
        text = re.sub(r'\s--\s', r' &mdash; ', text)
        text = re.sub(r'\n\.brk\n', r'\n<br/>\n', text)
        text = re.sub(r'\n\n+', r'\n<p>\n', text)

        contents = ''
        if len(html.toc) > 2:
            contents = '<p>\n<h2>Contents</h2>\n<p>\n<ul>\n'
            for n in range(0, len(html.toc)):
                contents += '<li><a href="#{}">{}</a>\n'.format(n, html.toc[n])
            contents += '</ul>'

        top = html.header.format(title=html.title, style=html.stylesheet)
        return top + html.synopsis + contents + text + html.bottom

#
#
#

generators = {
    'man': man.generate,
    'html': html.generate,
    'text': text.generate,
}

def main(argv):
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('-b', '--base', metavar='BASE', type=str)
    parser.add_argument('--man', type=str, default=None, const='', nargs='?')
    parser.add_argument('--html', type=str, default=None, const='', nargs='?')
    parser.add_argument('--text', type=str, default=None, const='', nargs='?')
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--help', action='store_true')
    parser.add_argument('--version', action='store_true')
    parser.add_argument('filename', nargs='?', type=str, default='-')
    args = parser.parse_args(argv[1:])
    if args.help:
        sys.stdout.write(usagestring)
        sys.exit()
    if args.version:
        sys.stdout.write(versionstring)
        sys.exit()
    if not args.filename or args.filename == '-':
        infilename = 'libredo'
        infile = sys.stdin
    else:
        infilename = args.filename
        infile = open(infilename, 'r')
    if args.base:
        basename = args.base
    else:
        basename = infilename
        n = basename.rfind('.')
        if n > 0:
            basename = basename[0:n]
    formats = {}
    if args.all or not args.man is None:
        formats['man'] = args.man or basename + '.3'
    if args.all or not args.html is None:
        formats['html'] = args.html or basename + '.html'
    if args.all or not args.text is None:
        formats['text'] = args.text or basename + '.txt'

    input = infile.read()
    for fmt in formats.keys():
        with open(formats[fmt], 'w') as outfile:
            outfile.write(generators[fmt](input))

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
