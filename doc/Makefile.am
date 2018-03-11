
DOC_FILES= \
    cndoc.tex \
    Doxyfile.in \
    genfooter.tex \
    genheader.tex \
    Makefile.am \
    Makefile.in \
    $(NULL)

EXTRA_DIST= \
    $(DOC_FILES) \
    $(NULL)

if ENABLE_DOXYGEN_DOC

all-local: doxygen.stamp

#doxygen.stamp: $(top_srcdir)/include/*.h $(top_srcdir)/src/*.c $(top_srcdir)/src/*.h
doxygen.stamp: $(top_srcdir)/include/*.h $(top_srcdir)/src/*.c
	$(DOXYGEN)
	touch doxygen.stamp

install-data-local: install-html

install-html:
	$(INSTALL) -d $(DESTDIR)$(htmldocdir)
	$(INSTALL_DATA) html/* $(DESTDIR)$(htmldocdir)

uninstall-local:
	rm -rf $(DESTDIR)$(htmldocdir)

clean-local:
	rm -rf html latex man
	rm -f doxygen.stamp

else

install-data-local:
uninstall-local:
clean-local:

endif

