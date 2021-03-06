#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sqlite3.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "books-collection.h"
#include "books-removed-dialog.h"


G_DEFINE_TYPE(BooksCollection, books_collection, G_TYPE_OBJECT)

#define BOOKS_COLLECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), BOOKS_TYPE_COLLECTION, BooksCollectionPrivate))

static void   set_pixbuf_column_from_file (BooksCollectionPrivate *priv, GtkTreeIter *iter, const gchar *cover);
static gchar *get_author_title_markup     (const gchar *author, const gchar *title);

enum {
    PROP_0,
    PROP_FILTER_TERM
};

struct _BooksCollectionPrivate {
    GtkListStore    *store;
    GtkTreeModel    *sorted;
    GtkTreeModel    *filtered;
    sqlite3         *db;
    gchar           *filter_term;
    GdkPixbuf       *placeholder;
};

BooksCollection *
books_collection_new (void)
{
    return BOOKS_COLLECTION (g_object_new (BOOKS_TYPE_COLLECTION, NULL));
}

GtkTreeModel *
books_collection_get_model (BooksCollection *collection)
{
    g_return_val_if_fail (BOOKS_IS_COLLECTION (collection), NULL);
    return collection->priv->sorted;
}

void
books_collection_add_book (BooksCollection *collection,
                           BooksEpub *epub,
                           const gchar *path)
{
    BooksCollectionPrivate *priv;
    GtkTreeIter iter;
    gchar *markup;
    const gchar *author;
    const gchar *title;
    const gchar *cover;
    const gchar *empty = "";
    const gchar *insert_sql = "INSERT INTO books (author, title, path, cover) VALUES (?, ?, ?, ?)";
    sqlite3_stmt *insert_stmt = NULL;

    g_return_if_fail (BOOKS_IS_COLLECTION (collection));

    priv = collection->priv;
    author = books_epub_get_meta (epub, "creator");
    title = books_epub_get_meta (epub, "title");
    cover = books_epub_get_cover (epub);
    markup = get_author_title_markup (author, title);

    if (author == NULL)
        author = g_strdup ("n/a");

    gtk_list_store_append (priv->store, &iter);
    gtk_list_store_set (priv->store, &iter,
                        BOOKS_COLLECTION_AUTHOR_COLUMN, author,
                        BOOKS_COLLECTION_TITLE_COLUMN, title,
                        BOOKS_COLLECTION_PATH_COLUMN, path,
                        BOOKS_COLLECTION_MARKUP_COLUMN, markup,
                        -1);

    set_pixbuf_column_from_file (priv, &iter, cover);

    sqlite3_prepare_v2 (priv->db, insert_sql, -1, &insert_stmt, NULL);
    sqlite3_bind_text (insert_stmt, 1, author, strlen (author), NULL);
    sqlite3_bind_text (insert_stmt, 2, title, strlen (title), NULL);
    sqlite3_bind_text (insert_stmt, 3, path, strlen (path), NULL);

    if (cover != NULL)
        sqlite3_bind_text (insert_stmt, 4, cover, strlen (cover), NULL);
    else
        sqlite3_bind_text (insert_stmt, 4, empty, strlen (empty), NULL);

    sqlite3_step (insert_stmt);
    sqlite3_finalize (insert_stmt);
    g_free (markup);
}

void
books_collection_remove_book (BooksCollection *collection,
                              GtkTreeIter *iter)
{
    BooksCollectionPrivate *priv;
    GtkTreeIter filtered_iter;
    GtkTreeIter real_iter;
    gchar *path;
    const gchar *remove_sql = "DELETE FROM books WHERE path=?";
    sqlite3_stmt *remove_stmt = NULL;

    g_return_if_fail (BOOKS_IS_COLLECTION (collection));
    priv = collection->priv;

    gtk_tree_model_get (priv->sorted, iter, BOOKS_COLLECTION_PATH_COLUMN, &path, -1);
    gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (priv->sorted), &filtered_iter, iter);
    gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (priv->filtered), &real_iter, &filtered_iter);
    gtk_list_store_remove (priv->store, &real_iter);
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filtered));

    /*
     * TODO: sqlite operations are noticeable. We should execute them
     * asynchronously.
     */
    sqlite3_prepare_v2 (priv->db, remove_sql, -1, &remove_stmt, NULL);
    sqlite3_bind_text (remove_stmt, 1, path, strlen (path), NULL);
    sqlite3_step (remove_stmt);
    sqlite3_finalize (remove_stmt);

    g_free (path);
}

BooksEpub *
books_collection_get_book (BooksCollection *collection,
                           GtkTreePath *path,
                           GError **error)
{
    BooksCollectionPrivate *priv;
    GtkTreePath *filtered_path;
    GtkTreePath *real_path;
    GtkTreeIter iter;

    g_return_val_if_fail (BOOKS_IS_COLLECTION (collection), NULL);

    priv = collection->priv;
    filtered_path = gtk_tree_model_sort_convert_path_to_child_path (GTK_TREE_MODEL_SORT (priv->sorted),
                                                                    path);

    real_path = gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (priv->filtered),
                                                                  filtered_path);

    if (gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->store), &iter, real_path)) {
        BooksEpub *epub;
        gchar *filename;

        gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter, BOOKS_COLLECTION_PATH_COLUMN, &filename, -1);
        epub = books_epub_new ();

        if (books_epub_open (epub, filename, error))
            return epub;
        else
            g_object_unref (epub);

        g_free (filename);
    }

    return NULL;
}

static void
set_pixbuf_column_from_file (BooksCollectionPrivate *priv,
                             GtkTreeIter *iter,
                             const gchar *cover)
{
    GdkPixbuf *pixbuf = priv->placeholder;

    if (cover != NULL && strlen (cover) > 0) {
        GError *error = NULL;

        pixbuf = gdk_pixbuf_new_from_file_at_size (cover, 64, -1, &error);

        if (error != NULL) {
            g_printerr (_("Could not load cover image: %s\n"), error->message);
            pixbuf = priv->placeholder;
        }
    }

    gtk_list_store_set (priv->store, iter, BOOKS_COLLECTION_ICON_COLUMN, pixbuf, -1);
}

static gchar *
get_author_title_markup (const gchar *author,
                         const gchar *title)
{
    return g_markup_printf_escaped ("%s &#8212; <i>%s</i>", author, title);
}

static void
create_db (BooksCollectionPrivate *priv)
{
    gchar *config_path;
    gchar *db_path;
    gchar *db_error;

    /* Make sure the path exists */
    config_path = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir(), "books", NULL);

    if (!g_file_test (config_path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
        g_mkdir (config_path, 0700);

    db_path = g_build_filename (config_path, "meta.db", NULL);
    g_assert (sqlite3_open (db_path, &priv->db) == SQLITE_OK);

    if (sqlite3_exec (priv->db,
                      "CREATE TABLE IF NOT EXISTS books (author TEXT, title TEXT, path TEXT, cover TEXT)",
                      NULL, NULL, &db_error)) {
        g_warning (_("Could not create table: %s\n"), db_error);
        sqlite3_free (db_error);
    }

    g_free (db_path);
    g_free (config_path);
}

static int
test_missing_book (gpointer user_data,
                   gint argc,
                   gchar **argv,
                   gchar **column)
{
    GPtrArray *missing_books;
    gchar *filename;
    filename = argv[0];
    missing_books = (GPtrArray *) user_data;

    if (!g_file_test (filename, G_FILE_TEST_EXISTS))
        g_ptr_array_add (missing_books, g_strdup (filename));

    return 0;
}

static void
remove_missing_books_from_db (BooksCollectionPrivate *priv)
{
    GPtrArray *missing_books;
    guint i;
    const gchar *delete_sql = "DELETE FROM books WHERE path=?";
    sqlite3_stmt *delete_stmt = NULL;

    missing_books = g_ptr_array_new_with_free_func ((GDestroyNotify) g_free);
    sqlite3_exec (priv->db, "SELECT path FROM books", test_missing_book, missing_books, NULL);

    sqlite3_prepare_v2 (priv->db, delete_sql, -1, &delete_stmt, NULL);

    for (i = 0; i < missing_books->len; i++) {
        gchar *filename;

        filename = (gchar *) g_ptr_array_index (missing_books, i);
        sqlite3_bind_text (delete_stmt, 1, filename, strlen (filename), NULL);
        sqlite3_step (delete_stmt);
        sqlite3_reset (delete_stmt);
    }

    sqlite3_finalize (delete_stmt);

    if (missing_books->len > 0) {
       GtkDialog *dialog;
       GtkListStore *model;

       model = gtk_list_store_new (1, G_TYPE_STRING);

       for (i = 0; i < missing_books->len; i++) {
           gchar *filename;
           GtkTreeIter iter;

           filename = (gchar *) g_ptr_array_index (missing_books, i);
           gtk_list_store_append (model, &iter);
           gtk_list_store_set (model, &iter, 0, filename, -1);
       }

       dialog = books_removed_dialog_new (GTK_TREE_MODEL (model));
       gtk_dialog_run (dialog);
    }

    g_ptr_array_free (missing_books, TRUE);
}

static int
insert_row_into_model (gpointer user_data,
                       gint argc,
                       gchar **argv,
                       gchar **column)
{
    BooksCollectionPrivate *priv;
    GtkTreeIter iter;
    gchar *author;
    gchar *title;
    gchar *cover;
    gchar *markup;

    g_assert (argc == 4);
    priv = (BooksCollectionPrivate *) user_data;
    author = argv[0];
    title = argv[1];
    cover = argv[3];
    markup = get_author_title_markup (author, title);

    gtk_list_store_append (priv->store, &iter);
    gtk_list_store_set (priv->store, &iter,
                        BOOKS_COLLECTION_AUTHOR_COLUMN, author,
                        BOOKS_COLLECTION_TITLE_COLUMN, title,
                        BOOKS_COLLECTION_MARKUP_COLUMN, markup,
                        BOOKS_COLLECTION_PATH_COLUMN, argv[2],
                        -1);

    set_pixbuf_column_from_file (priv, &iter, cover);
    g_free (markup);
    return 0;
}

static void
insert_books_from_db_into_model (BooksCollectionPrivate *priv)
{
    gchar *db_error;

    if (sqlite3_exec (priv->db, "SELECT author, title, path, cover FROM books",
                      insert_row_into_model, priv, &db_error)) {
        g_warning (_("Could not select data: %s\n"), db_error);
        sqlite3_free (db_error);
    }
}

static gboolean
row_visible (GtkTreeModel *model,
             GtkTreeIter *iter,
             BooksCollectionPrivate *priv)
{
    gboolean visible;
    gchar *lowered_author;
    gchar *lowered_title;
    gchar *lowered_term;
    gchar *author;
    gchar *title;

    if (priv->filter_term == NULL)
        return TRUE;

    gtk_tree_model_get (model, iter,
                        BOOKS_COLLECTION_AUTHOR_COLUMN, &author,
                        BOOKS_COLLECTION_TITLE_COLUMN, &title,
                        -1);

    if (author == NULL || title == NULL)
        return TRUE;

    lowered_term = g_utf8_strdown (priv->filter_term, -1);
    lowered_author = g_utf8_strdown (author, -1);
    lowered_title = g_utf8_strdown (title, -1);

    visible = strstr (lowered_author, lowered_term) != NULL ||
              strstr (lowered_title, lowered_term) != NULL;

    g_free (lowered_author);
    g_free (lowered_title);
    g_free (lowered_term);
    g_free (author);
    g_free (title);

    return visible;
}

static void
books_collection_dispose (GObject *object)
{
    G_OBJECT_CLASS (books_collection_parent_class)->dispose (object);
}

static void
books_collection_finalize (GObject *object)
{
    BooksCollectionPrivate *priv;

    priv = BOOKS_COLLECTION_GET_PRIVATE (object);
    g_free (priv->filter_term);
    sqlite3_close (priv->db);

    G_OBJECT_CLASS (books_collection_parent_class)->finalize (object);
}

static void
books_collection_set_property (GObject *object,
                               guint property_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
    BooksCollectionPrivate *priv;

    priv = BOOKS_COLLECTION_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILTER_TERM:
            if (priv->filter_term != NULL)
                g_free (priv->filter_term);

            priv->filter_term = g_strdup (g_value_get_string (value));
            gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (priv->filtered));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
books_collection_get_property(GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
    BooksCollectionPrivate *priv;

    priv = BOOKS_COLLECTION_GET_PRIVATE (object);

    switch (property_id) {
        case PROP_FILTER_TERM:
            g_value_set_string (value, priv->filter_term);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}


static void
books_collection_class_init (BooksCollectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = books_collection_set_property;
    object_class->get_property = books_collection_get_property;
    object_class->dispose = books_collection_dispose;
    object_class->finalize = books_collection_finalize;

    g_object_class_install_property (object_class,
                                     PROP_FILTER_TERM,
                                     g_param_spec_string ("filter-term",
                                                          "Collection filter term",
                                                          "Collection filter term",
                                                          NULL,
                                                          G_PARAM_READWRITE));

    g_type_class_add_private (klass, sizeof(BooksCollectionPrivate));
}

static void
books_collection_init (BooksCollection *collection)
{
    BooksCollectionPrivate *priv;
    GInputStream *stream;
    GError *error = NULL;

    collection->priv = priv = BOOKS_COLLECTION_GET_PRIVATE (collection);
    priv->filter_term = NULL;

    /* Create pixbuf for unknown cover image */
    stream = g_resources_open_stream ("/com/github/matze/books/ui/book-cover.png", 0, &error);

    if (error != NULL) {
        g_error ("%s\n", error->message);
        g_error_free (error);
    }

    priv->placeholder = gdk_pixbuf_new_from_stream (stream, NULL, &error);

    if (error != NULL) {
        g_error ("%s\n", error->message);
        g_error_free (error);
    }

    g_input_stream_close (stream, NULL, NULL);

    /* Create model */
    priv->store = gtk_list_store_new (BOOKS_COLLECTION_N_COLUMNS,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING,
                                      GDK_TYPE_PIXBUF);

    priv->filtered = gtk_tree_model_filter_new (GTK_TREE_MODEL (priv->store), NULL);

    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (priv->filtered),
                                            (GtkTreeModelFilterVisibleFunc) row_visible,
                                            priv, NULL);

    priv->sorted = gtk_tree_model_sort_new_with_model (priv->filtered);

    /* Create database */
    create_db (priv);
    remove_missing_books_from_db (priv);
    insert_books_from_db_into_model (priv);
}
