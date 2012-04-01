-include ../../config.mak.autogen
-include ../../config.mak

# this should be set to a 'standard' bsd-type install program
INSTALL ?= install
INSTALL_DATA = $(INSTALL) -c -m 0644
INSTALL_EXE = $(INSTALL) -c -m 0755
INSTALL_DIR = $(INSTALL) -c -d -m 0755

ASCIIDOC_CONF      = ../../Documentation/asciidoc.conf
MANPAGE_NORMAL_XSL =  ../../Documentation/manpage-normal.xsl

GIT_SUBTREE_SH := git-subtree.sh
GIT_SUBTREE    := ../../git-subtree

all: $(GIT_SUBTREE)

$(GIT_SUBTREE): $(GIT_SUBTREE_SH)
	cp $< $@ && chmod +x $@

install: install-exe install-doc

install-exe: git-subtree.sh
	$(INSTALL_DIR) $(DESTDIR)/$(gitdir)
	$(INSTALL_EXE) $< $(DESTDIR)/$(gitdir)/git-subtree

install-doc: git-subtree.1
	$(INSTALL_DIR) $(DESTDIR)/$(mandir)/man1/
	$(INSTALL_DATA) $< $(DESTDIR)/$(mandir)/man1/

doc: git-subtree.1

%.1: %.xml
	xmlto -m $(MANPAGE_NORMAL)  man $^

%.xml: %.txt
	asciidoc -b docbook -d manpage -f $(ASCIIDOC_CONF) \
		-agit_version=$(gitver) $^

test:
	$(MAKE) -C t/ all

clean:
	rm -f *~ *.xml *.html *.1
	rm -rf subproj mainline
