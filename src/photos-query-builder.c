/*
 * Photos - access, organize and share your photos on GNOME
 * Copyright © 2012 – 2019 Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Based on code from:
 *   + Documents
 */


#include "config.h"

#include <string.h>

#include "photos-base-manager.h"
#include "photos-query-builder.h"
#include "photos-search-type.h"
#include "photos-source-manager.h"
#include "photos-search-match-manager.h"
#include "photos-search-type-manager.h"


static const gchar *BLOCKED_MIME_TYPES_FILTER = "(nie:mimeType(?urn) != 'image/gif' && nie:mimeType(?urn) != 'image/x-eps')";

static const gchar *COLLECTIONS_FILTER
  = "(fn:starts-with (nao:identifier (?urn), '" PHOTOS_QUERY_COLLECTIONS_IDENTIFIER "')"
    " || (?urn = nfo:image-category-screenshot))";


static gchar *
photos_query_builder_query (PhotosSearchContextState *state,
                            gboolean global,
                            gint flags,
                            PhotosOffsetController *offset_cntrlr)
{
  PhotosSparqlTemplate *sparql_template;
  const gchar *projection
    = "?urn "
      "nie:url (?urn) "
      "nfo:fileName (?urn) "
      "nie:mimeType (?urn) "
      "nie:title (?urn) "
      "tracker:coalesce (nco:fullname (?creator), nco:fullname (?publisher), '') "
      "tracker:coalesce (nfo:fileLastModified (?urn), nie:contentLastModified (?urn)) AS ?mtime "
      "nao:identifier (?urn) "
      "rdf:type (?urn) "
      "nie:dataSource(?urn) "
      "( EXISTS { ?urn nao:hasTag nao:predefined-tag-favorite } ) "
      "( EXISTS { ?urn nco:contributor ?contributor FILTER ( ?contributor != ?creator ) } ) "
      "tracker:coalesce(nfo:fileCreated (?urn), nie:contentCreated (?urn)) "
      "nfo:width (?urn) "
      "nfo:height (?urn) "
      "nfo:equipment (?urn) "
      "nfo:orientation (?urn) "
      "nmm:exposureTime (?urn) "
      "nmm:fnumber (?urn) "
      "nmm:focalLength (?urn) "
      "nmm:isoSpeed (?urn) "
      "nmm:flash (?urn) "
      "slo:location (?urn) ";
  g_autofree gchar *item_mngr_where = NULL;
  g_autofree gchar *srch_mtch_mngr_filter = NULL;
  g_autofree gchar *src_mngr_filter = NULL;
  g_autofree gchar *offset_limit = NULL;
  gchar *sparql;

  sparql_template = photos_base_manager_get_sparql_template (state->srch_typ_mngr, flags);

  if (!(flags & PHOTOS_QUERY_FLAGS_UNFILTERED))
    {
      if (global)
        {
          /* TODO: SearchCategoryManager */

          item_mngr_where = photos_base_manager_get_where (state->item_mngr, flags);
        }

      src_mngr_filter = photos_base_manager_get_filter (state->src_mngr, flags);
      srch_mtch_mngr_filter = photos_base_manager_get_filter (state->srch_mtch_mngr, flags);
    }

  if (global && (flags & PHOTOS_QUERY_FLAGS_UNLIMITED) == 0)
    {
      gint offset = 0;
      gint step = 60;

      if (offset_cntrlr != NULL)
        {
          offset = photos_offset_controller_get_offset (offset_cntrlr);
          step = photos_offset_controller_get_step (offset_cntrlr);
        }

      offset_limit = g_strdup_printf ("LIMIT %d OFFSET %d", step, offset);
    }

  sparql
    = photos_sparql_template_get_sparql (sparql_template,
                                         "blocked_mime_types_filter", BLOCKED_MIME_TYPES_FILTER,
                                         "collections_filter", COLLECTIONS_FILTER,
                                         "item_where", item_mngr_where == NULL ? "" : item_mngr_where,
                                         "order", "ORDER BY DESC (?mtime)",
                                         "offset_limit", offset_limit ? offset_limit : "",
                                         "projection", projection,
                                         "search_match_filter", srch_mtch_mngr_filter == NULL ? "(true)" : srch_mtch_mngr_filter,
                                         "source_filter", src_mngr_filter == NULL ? "(true)" : src_mngr_filter,
                                         NULL);

  return sparql;
}


PhotosQuery *
photos_query_builder_create_collection_query (PhotosSearchContextState *state,
                                              const gchar *name,
                                              const gchar *identifier_tag)
{
  g_autoptr (GDateTime) now = NULL;
  PhotosQuery *query;
  g_autofree gchar *identifier = NULL;
  g_autofree gchar *sparql = NULL;
  g_autofree gchar *time = NULL;

  identifier = g_strdup_printf ("%s%s",
                                PHOTOS_QUERY_LOCAL_COLLECTIONS_IDENTIFIER,
                                identifier_tag == NULL ? name : identifier_tag);

  now = g_date_time_new_now_utc ();
  time = g_date_time_format_iso8601 (now);

  sparql = g_strdup_printf ("INSERT { _:res a nfo:DataContainer ; a nie:DataObject ; "
                            "nie:contentLastModified '%s' ; "
                            "nie:title '%s' ; "
                            "nao:identifier '%s' }",
                            time,
                            name,
                            identifier);

  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_collection_icon_query (PhotosSearchContextState *state, const gchar *resource)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = g_strdup_printf ("SELECT ?urn "
                            "tracker:coalesce(nfo:fileLastModified(?urn), nie:contentLastModified(?urn)) AS ?mtime "
                            "WHERE { ?urn nie:isPartOf <%s> } "
                            "ORDER BY DESC (?mtime) LIMIT 4",
                            resource);

  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_count_query (PhotosSearchContextState *state, gint flags)
{
  PhotosQuery *query;
  PhotosSparqlTemplate *sparql_template;
  g_autofree gchar *item_mngr_where = NULL;
  g_autofree gchar *src_mngr_filter = NULL;
  g_autofree gchar *srch_mtch_mngr_filter = NULL;
  g_autofree gchar *sparql = NULL;

  sparql_template = photos_base_manager_get_sparql_template (state->srch_typ_mngr, flags);

  if ((flags & PHOTOS_QUERY_FLAGS_UNFILTERED) == 0)
    {
      item_mngr_where = photos_base_manager_get_where (state->item_mngr, flags);
      src_mngr_filter = photos_base_manager_get_filter (state->src_mngr, flags);
      srch_mtch_mngr_filter = photos_base_manager_get_filter (state->srch_mtch_mngr, flags);
    }

  sparql
    = photos_sparql_template_get_sparql (sparql_template,
                                         "blocked_mime_types_filter", BLOCKED_MIME_TYPES_FILTER,
                                         "collections_filter", COLLECTIONS_FILTER,
                                         "item_where", item_mngr_where == NULL ? "" : item_mngr_where,
                                         "order", "",
                                         "offset_limit", "",
                                         "projection", "COUNT(?urn)",
                                         "search_match_filter", srch_mtch_mngr_filter == NULL ? "(true)" : srch_mtch_mngr_filter,
                                         "source_filter", src_mngr_filter == NULL ? "(true)" : src_mngr_filter,
                                         NULL);

  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_delete_resource_query (PhotosSearchContextState *state, const gchar *resource)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = g_strdup_printf ("DELETE { <%s> a rdfs:Resource }", resource);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_equipment_query (PhotosSearchContextState *state, GQuark equipment)
{
  PhotosQuery *query;
  const gchar *resource;
  g_autofree gchar *sparql = NULL;

  resource = g_quark_to_string (equipment);
  sparql = g_strdup_printf ("SELECT nfo:manufacturer (<%s>) nfo:model (<%s>) WHERE {}", resource, resource);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_fetch_collections_for_urn_query (PhotosSearchContextState *state, const gchar *resource)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = g_strdup_printf ("SELECT ?urn WHERE { ?urn a nfo:DataContainer . <%s> nie:isPartOf ?urn }", resource);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_fetch_collections_local (PhotosSearchContextState *state)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = photos_query_builder_query (state,
                                       TRUE,
                                       PHOTOS_QUERY_FLAGS_COLLECTIONS
                                       | PHOTOS_QUERY_FLAGS_LOCAL
                                       | PHOTOS_QUERY_FLAGS_UNLIMITED,
                                       NULL);

  query = photos_query_new (NULL, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_global_query (PhotosSearchContextState *state,
                                   gint flags,
                                   PhotosOffsetController *offset_cntrlr)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = photos_query_builder_query (state, TRUE, flags, offset_cntrlr);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_location_query (PhotosSearchContextState *state, const gchar *location_urn)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = g_strdup_printf ("SELECT slo:latitude (<%s>) slo:longitude (<%s>) WHERE {}", location_urn, location_urn);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_set_collection_query (PhotosSearchContextState *state,
                                           const gchar *item_urn,
                                           const gchar *collection_urn,
                                           gboolean setting)
{
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;

  sparql = g_strdup_printf ("%s { <%s> nie:isPartOf <%s> }",
                            setting ? "INSERT" : "DELETE",
                            item_urn,
                            collection_urn);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_single_query (PhotosSearchContextState *state, gint flags, const gchar *resource)
{
  g_autoptr (GRegex) regex = NULL;
  PhotosQuery *query;
  g_autofree gchar *replacement = NULL;
  g_autofree gchar *sparql = NULL;
  g_autofree gchar *tmp = NULL;

  tmp = photos_query_builder_query (state, FALSE, flags, NULL);

  regex = g_regex_new ("\\?urn", 0, 0, NULL);
  replacement = g_strconcat ("<", resource, ">", NULL);
  sparql = g_regex_replace (regex, tmp, -1, 0, replacement, 0, NULL);
  query = photos_query_new (state, sparql);

  return query;
}


PhotosQuery *
photos_query_builder_update_mtime_query (PhotosSearchContextState *state, const gchar *resource)
{
  g_autoptr (GDateTime) now = NULL;
  PhotosQuery *query;
  g_autofree gchar *sparql = NULL;
  g_autofree gchar *time = NULL;

  now = g_date_time_new_now_utc ();
  time = g_date_time_format_iso8601 (now);

  sparql = g_strdup_printf ("INSERT OR REPLACE { <%s> nie:contentLastModified '%s' }", resource, time);
  query = photos_query_new (state, sparql);

  return query;
}
