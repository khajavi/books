ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

AM_CPPFLAGS = 						\
		-Wall -Werror 				\
		$(BOOKS_CFLAGS) 			\
		-DDATADIR=\""$(datadir)"\"

bin_PROGRAMS = books

BUILT_SOURCES_PRIVATE = 	\
		books-resources.c

books_SOURCES = 					\
		main.c 						\
		books-collection.c 			\
		books-collection.h 			\
		books-epub.c 				\
		books-epub.h 				\
		books-window.c 				\
		books-window.h 				\
		books-main-window.c 		\
		books-main-window.h 		\
		books-preferences-dialog.c 	\
		books-preferences-dialog.h 	\
		books-removed-dialog.c 		\
		books-removed-dialog.h 		\
		$(BUILT_SOURCES_PRIVATE)

books_LDADD = $(BOOKS_LIBS)

RESOURCES = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/books.gresource.xml)

books-resources.c: books.gresource.xml $(RESOURCES)
	$(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate --c-name books $(srcdir)/books.gresource.xml

EXTRA_DIST = 							\
		books.gresource.xml 			\
		ui/books.xml 					\
		ui/book-cover.png 				\
		ui/books-preferences-dialog.ui

CLEANFILES = $(BUILT_SOURCES_PRIVATE)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES_PRIVATE)
