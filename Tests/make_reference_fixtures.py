from pathlib import Path
from docx import Document
from openpyxl import Workbook
from pptx import Presentation
from reportlab.pdfgen.canvas import Canvas

folder = Path(__file__).resolve().parents[1] / '.build' / 'windows' / 'reference-fixtures'
folder.mkdir(parents=True, exist_ok=True)
content = 'The launch code for project Firefly is Cobalt317.'
for ext in ['txt', 'md', 'csv']:
    (folder / ('sample.' + ext)).write_text(content, encoding='utf-8')
(folder / 'sample.html').write_text('<html><body><p>' + content + '</p></body></html>')
(folder / 'sample.rtf').write_text(r'{\rtf1\ansi ' + content + '}')
doc = Document()
doc.add_paragraph(content)
doc.save(folder / 'sample.docx')
wb = Workbook()
wb.active.append(['Launch code', content])
wb.save(folder / 'sample.xlsx')
slides = Presentation()
s = slides.slides.add_slide(slides.slide_layouts[1])
s.placeholders[1].text = content
slides.save(folder / 'sample.pptx')
pdf = Canvas(str(folder / 'sample.pdf'))
pdf.drawString(40, 750, content)
pdf.save()
