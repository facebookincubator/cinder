PANDOC        = stack exec pandoc -- 
MATHJAX       = "http://cdn.mathjax.org/mathjax/latest/MathJax.js?config=TeX-AMS-MML_HTMLorMML"
FLAGS         = --standalone --toc --toc-depth=2 --mathjax=$(MATHJAX) --highlight-style pygments
PNG_IMAGES    = $(patsubst %.pdf,%.png,$(wildcard img/*.pdf))
IFORMAT       = -f gfm
FILTER        = includes.exe
FILTER_OPT    = --filter=${FILTER}  # includes.hs 

############################### html
STYLE         = css/style.css
TEMPLATE_HTML = template.html
HTML_OPT      = -c ${STYLE} --template ${TEMPLATE_HTML} -t html
PGTITLE       = --metadata pagetitle="The Parallel Hashmap" 

############################### pdf
TEMPLATE_TEX  = template.latex
TEX_OPT       = --template $(TEMPLATE_TEX) --pdf-engine=xelatex

############################### epub
EPUB_COVER    = --epub-cover-image=img/cover-kindle.jpg


SRC = parallel_hashmap.md

OBJ = $(SRC:.md=.html)

all: html

includes.exe: includes.hs
	stack exec ghc -- -o $@ -no-keep-hi-files -no-keep-o-files includes.hs

html: parallel_hashmap.md $(FILTER) ${TEMPLATE_HTML} ${STYLE}
	$(PANDOC) ${FILTER_OPT} ${IFORMAT} ${HTML_OPT} $(FLAGS) ${PGTITLE} -o ../index.html parallel_hashmap.md

%.pdf: %.md $(FILTER) ${TEMPLATE_TEX}
	$(PANDOC) ${FILTER_OPT} ${IFORMAT} ${TEX_OPT} $(FLAGS) -o $@ $<

pdf: $(FILTER) ${TEMPLATE_TEX} 
	rm -f parallel_hashmap.pdf; $(PANDOC) ${FILTER_OPT} ${IFORMAT} ${TEX_OPT} $(FLAGS) -o parallel_hashmap.pdf title.md $(SRC)

native: 
	$(PANDOC) -s -t native $(SRC)

native_filt: $(FILTER)
	$(PANDOC) ${FILTER_OPT} -s -t native $(SRC)

clean:
	-rm -f *.html *.pdf cppi.epub
