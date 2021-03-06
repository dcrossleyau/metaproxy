/* This file is part of Metaproxy.
   Copyright (C) Index Data

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "config.hpp"

#include <stdlib.h>
#include <sys/types.h>
#include "filter_zoom.hpp"
#include <metaproxy/package.hpp>
#include <metaproxy/util.hpp>
#include <metaproxy/xmlutil.hpp>
#include <yaz/comstack.h>
#include <yaz/poll.h>
#include "torus.hpp"

#include <libxslt/xsltutils.h>
#include <libxslt/transform.h>

#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

#include <yaz/yaz-version.h>
#include <yaz/tpath.h>
#include <yaz/srw.h>
#include <yaz/ccl_xml.h>
#include <yaz/ccl.h>
#include <yaz/rpn2cql.h>
#include <yaz/rpn2solr.h>
#include <yaz/pquery.h>
#include <yaz/cql.h>
#include <yaz/oid_db.h>
#include <yaz/diagbib1.h>
#include <yaz/log.h>
#include <yaz/zgdu.h>
#include <yaz/querytowrbuf.h>
#include <yaz/sortspec.h>
#include <yaz/tokenizer.h>
#include <yaz/zoom.h>
#include <yaz/otherinfo.h>
#include <yaz/match_glob.h>

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {
        class Zoom::Searchable : boost::noncopyable {
          public:
            std::string authentication;
            std::string authenticationMode;
            std::string contentAuthentication;
            std::string cfAuth;
            std::string cfProxy;
            std::string cfSubDB;
            std::string udb;
            std::string target;
            std::string query_encoding;
            std::string sru;
            std::string sru_version;
            std::string request_syntax;
            std::string element_set;
            std::string record_encoding;
            std::string transform_xsl_fname;
            std::string transform_xsl_content;
            std::string urlRecipe;
            std::string contentConnector;
            std::string sortStrategy;
            std::string extraArgs;
            std::string rpn2cql_fname;
            std::string retry_on_failure;
            std::map<std::string, std::string> cf_param;
            bool use_turbomarc;
            bool piggyback;
            CCL_bibset ccl_bibset;
            std::map<std::string, std::string> sortmap;
            Searchable(CCL_bibset base);
            ~Searchable();
        };
        class Zoom::Backend : boost::noncopyable {
            friend class Impl;
            friend class Frontend;
            mp::wrbuf m_apdu_wrbuf;
            ZOOM_connection m_connection;
            ZOOM_resultset m_resultset;
            std::string m_frontend_database;
            SearchablePtr sptr;
            xsltStylesheetPtr xsp;
            std::string cproxy_host;
            bool enable_cproxy;
            bool enable_explain;
            xmlDoc *explain_doc;
            std::string m_proxy;
            cql_transform_t cqlt;
            std::string retry_on_failure;
        public:
            Backend();
            ~Backend();
            void connect(std::string zurl, int *error, char **addinfo,
                         ODR odr);
            void search(ZOOM_query q, Odr_int *hits,
                        int *error, char **addinfo, Z_FacetList **fl, ODR odr);
            void present(Odr_int start, Odr_int number, ZOOM_record *recs,
                         int *error, char **addinfo, ODR odr);
            void set_option(const char *name, const char *value);
            void set_option(const char *name, const char *value, size_t l);
            void set_option(const char *name, std::string value);
            const char *get_option(const char *name);
            void get_zoom_error(int *error, char **addinfo, ODR odr);
        };
        class Zoom::Frontend : boost::noncopyable {
            friend class Impl;
            Impl *m_p;
            bool m_is_virtual;
            bool m_in_use;
            std::string session_realm;
            yazpp_1::GDU m_init_gdu;
            BackendPtr m_backend;
            void handle_package(mp::Package &package);
            void handle_search(mp::Package &package);
            void auth(mp::Package &package, Z_InitRequest *req,
                      int *error, char **addinfo, ODR odr);

            BackendPtr explain_search(mp::Package &package,
                                      std::string &database,
                                      int *error,
                                      char **addinfo,
                                      mp::odr &odr,
                                      std::string torus_url,
                                      std::string &torus_db,
                                      std::string &realm);
            void handle_present(mp::Package &package);
            BackendPtr get_backend_from_databases(mp::Package &package,
                                                  std::string &database,
                                                  int *error,
                                                  char **addinfo,
                                                  mp::odr &odr,
                                                  int *proxy_step);

            bool create_content_session(mp::Package &package,
                                        BackendPtr b,
                                        int *error,
                                        char **addinfo,
                                        ODR odr,
                                        std::string authentication,
                                        std::string proxy,
                                        std::string realm);

            void prepare_elements(BackendPtr b,
                                  Odr_oid *preferredRecordSyntax,
                                  const char *element_set_name,
                                  bool &enable_pz2_retrieval,
                                  bool &enable_pz2_transform,
                                  bool &enable_record_transform,
                                  bool &assume_marc8_charset);

            Z_Records *get_records(Package &package,
                                   Odr_int start,
                                   Odr_int number_to_present,
                                   int *error,
                                   char **addinfo,
                                   Odr_int *number_of_records_returned,
                                   ODR odr, BackendPtr b,
                                   Odr_oid *preferredRecordSyntax,
                                   const char *element_set_name);
            Z_Records *get_explain_records(Package &package,
                                           Odr_int start,
                                           Odr_int number_to_present,
                                           int *error,
                                           char **addinfo,
                                           Odr_int *number_of_records_returned,
                                           ODR odr, BackendPtr b,
                                           Odr_oid *preferredRecordSyntax,
                                           const char *element_set_name);
            bool retry(mp::Package &package,
                       mp::odr &odr,
                       BackendPtr b,
                       int &error, char **addinfo,
                       int &proxy_step, int &same_retries,
                       int &proxy_retries);
            void log_diagnostic(mp::Package &package,
                                int error, const char *addinfo);
        public:
            Frontend(Impl *impl);
            ~Frontend();
        };
        class Zoom::Impl {
            friend class Frontend;
        public:
            Impl();
            ~Impl();
            void process(metaproxy_1::Package & package);
            void configure(const xmlNode * ptr, bool test_only,
                           const char *path);
        private:
            void configure_local_records(const xmlNode * ptr, bool test_only);
            bool check_proxy(const char *proxy);



            FrontendPtr get_frontend(mp::Package &package);
            void release_frontend(mp::Package &package);
            SearchablePtr parse_torus_record(const xmlNode *ptr);
            struct cql_node *convert_cql_fields(struct cql_node *cn, ODR odr);
            std::map<mp::Session, FrontendPtr> m_clients;
            boost::mutex m_mutex;
            boost::condition m_cond_session_ready;
            std::string torus_searchable_url;
            std::string torus_content_url;
            std::string torus_auth_url;
            std::string torus_allow_ip;
            std::string default_realm;
            std::string torus_auth_hostname;
            std::map<std::string,std::string> fieldmap;
            std::string xsldir;
            std::string file_path;
            std::string content_proxy_server;
            std::string content_tmp_file;
            std::string content_config_file;
            bool apdu_log;
            CCL_bibset bibset;
            std::string element_transform;
            std::string element_raw;
            std::string element_passthru;
            std::string proxy;
            xsltStylesheetPtr explain_xsp;
            xsltStylesheetPtr record_xsp;
            std::map<std::string,SearchablePtr> s_map;
            std::string zoom_timeout;
            int proxy_timeout;
        };
    }
}


static xmlNode *xml_node_search(xmlNode *ptr, int *num, int m)
{
    while (ptr)
    {
        if (ptr->type == XML_ELEMENT_NODE &&
            !strcmp((const char *) ptr->name, "recordData"))
        {
            (*num)++;
            if (m == *num)
                return ptr;
        }
        else  // else: we don't want to find nested nodes
        {
            xmlNode *ret_node = xml_node_search(ptr->children, num, m);
            if (ret_node)
                return ret_node;
        }
        ptr = ptr->next;
    }
    return 0;
}

// define Pimpl wrapper forwarding to Impl

yf::Zoom::Zoom() : m_p(new Impl)
{
}

yf::Zoom::~Zoom()
{  // must have a destructor because of boost::scoped_ptr
}

void yf::Zoom::configure(const xmlNode *xmlnode, bool test_only,
                         const char *path)
{
    m_p->configure(xmlnode, test_only, path);
}

void yf::Zoom::process(mp::Package &package) const
{
    m_p->process(package);
}


// define Implementation stuff

yf::Zoom::Backend::Backend()
{
    m_connection = ZOOM_connection_create(0);
    ZOOM_connection_save_apdu_wrbuf(m_connection, m_apdu_wrbuf);
    m_resultset = 0;
    xsp = 0;
    enable_cproxy = true;
    enable_explain = false;
    explain_doc = 0;
    cqlt = 0;
}

yf::Zoom::Backend::~Backend()
{
    if (xsp)
        xsltFreeStylesheet(xsp);
    if (explain_doc)
        xmlFreeDoc(explain_doc);
    cql_transform_close(cqlt);
    ZOOM_connection_destroy(m_connection);
    ZOOM_resultset_destroy(m_resultset);
}


void yf::Zoom::Backend::get_zoom_error(int *error, char **addinfo,
                                       ODR odr)
{
    const char *msg = 0;
    const char *zoom_addinfo = 0;
    const char *dset = 0;
    int error0 = ZOOM_connection_error_x(m_connection, &msg,
                                         &zoom_addinfo, &dset);
    if (error0)
    {
        if (!dset)
            dset = "Unknown";

        if (!strcmp(dset, "info:srw/diagnostic/1"))
            *error = yaz_diag_srw_to_bib1(error0);
        else if (!strcmp(dset, "Bib-1"))
            *error = error0;
        else if (!strcmp(dset, "ZOOM"))
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            if (error0 == ZOOM_ERROR_INIT)
                *error = YAZ_BIB1_INIT_AC_AUTHENTICATION_SYSTEM_ERROR;
            else if (error0 == ZOOM_ERROR_DECODE)
            {
                if (zoom_addinfo)
                {
                    if (strstr(zoom_addinfo, "Authentication") ||
                        strstr(zoom_addinfo, "authentication"))
                        *error = YAZ_BIB1_INIT_AC_AUTHENTICATION_SYSTEM_ERROR;
                }
            }
        }
        else
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;

        *addinfo = (char *) odr_malloc(
            odr, 30 + strlen(dset) + strlen(msg) +
            (zoom_addinfo ? strlen(zoom_addinfo) : 0));
        **addinfo = '\0';
        if (zoom_addinfo && *zoom_addinfo)
        {
            strcpy(*addinfo, zoom_addinfo);
            strcat(*addinfo, " ");
        }
        sprintf(*addinfo + strlen(*addinfo), "(%s %d %s)", dset, error0, msg);
    }
}

void yf::Zoom::Backend::connect(std::string zurl,
                                int *error, char **addinfo,
                                ODR odr)
{
    size_t h = zurl.find_first_of('#');
    if (h != std::string::npos)
        zurl.erase(h);
    ZOOM_connection_connect(m_connection, zurl.length() ? zurl.c_str() : 0, 0);
    get_zoom_error(error, addinfo, odr);

}

void yf::Zoom::Backend::search(ZOOM_query q, Odr_int *hits,
                               int *error, char **addinfo, Z_FacetList **flp,
                               ODR odr)
{
    ZOOM_resultset_destroy(m_resultset);
    m_resultset = 0;
    if (*flp)
    {
        WRBUF w = wrbuf_alloc();
        yaz_facet_list_to_wrbuf(w, *flp);
        set_option("facets", wrbuf_cstr(w));
        wrbuf_destroy(w);
    }
    m_resultset = ZOOM_connection_search(m_connection, q);
    get_zoom_error(error, addinfo, odr);
    if (*error == 0)
        *hits = ZOOM_resultset_size(m_resultset);
    else
        *hits = 0;
    *flp = 0;
    size_t num_facets = ZOOM_resultset_facets_size(m_resultset);
    if (num_facets > 0)
    {
        size_t i;
        Z_FacetList *fl = (Z_FacetList *) odr_malloc(odr, sizeof(*fl));
        fl->elements = (Z_FacetField **)
            odr_malloc(odr, num_facets * sizeof(*fl->elements));
        for (i = 0; i < num_facets; i++)
        {
            ZOOM_facet_field ff =
                ZOOM_resultset_get_facet_field_by_index(m_resultset, i);
            if (!ff)
                break;
            Z_AttributeList *al = (Z_AttributeList *)
                odr_malloc(odr, sizeof(*al));
            al->num_attributes = 1;
            al->attributes = (Z_AttributeElement **)
                odr_malloc(odr, sizeof(*al->attributes));
            Z_AttributeElement *ae = al->attributes[0] = (Z_AttributeElement *)
                odr_malloc(odr, sizeof(**al->attributes));
            ae->attributeSet = 0;
            ae->attributeType = odr_intdup(odr, 1);
            ae->which = Z_AttributeValue_complex;
            ae->value.complex = (Z_ComplexAttribute *)
                odr_malloc(odr, sizeof(*ae->value.complex));
            ae->value.complex->num_list = 1;
            ae->value.complex->list = (Z_StringOrNumeric **)
                odr_malloc(odr, sizeof(**ae->value.complex->list));
            ae->value.complex->list[0] = (Z_StringOrNumeric *)
                odr_malloc(odr, sizeof(**ae->value.complex->list));
            ae->value.complex->list[0]->which = Z_StringOrNumeric_string;
            ae->value.complex->list[0]->u.string =
                odr_strdup(odr, ZOOM_facet_field_name(ff));
            ae->value.complex->num_semanticAction = 0;
            ae->value.complex->semanticAction = 0;

            int num_terms = ZOOM_facet_field_term_count(ff);
            fl->elements[i] = (Z_FacetField *)
                odr_malloc(odr, sizeof(Z_FacetField));
            fl->elements[i]->attributes = al;
            fl->elements[i]->num_terms = num_terms;
            fl->elements[i]->terms = (Z_FacetTerm **)
                odr_malloc(odr, num_terms * sizeof(Z_FacetTerm *));
            int j;
            for (j = 0; j < num_terms; j++)
            {
                int freq;
                const char *a_term = ZOOM_facet_field_get_term(ff, j, &freq);
                Z_FacetTerm *ft = (Z_FacetTerm *) odr_malloc(odr, sizeof(*ft));
                ft->term = z_Term_create(odr, Z_Term_general, a_term,
                                         strlen(a_term));
                ft->count = odr_intdup(odr, freq);

                fl->elements[i]->terms[j] = ft;
            }
        }
        fl->num = i;
        *flp = fl;
    }
}

void yf::Zoom::Backend::present(Odr_int start, Odr_int number,
                                ZOOM_record *recs,
                                int *error, char **addinfo, ODR odr)
{
    ZOOM_resultset_records(m_resultset, recs, start, number);
    get_zoom_error(error, addinfo, odr);
}


void yf::Zoom::Backend::set_option(const char *name, const char *value, size_t l)
{
    ZOOM_connection_option_setl(m_connection, name, value, l);
}

void yf::Zoom::Backend::set_option(const char *name, const char *value)
{
    ZOOM_connection_option_set(m_connection, name, value);
    if (m_resultset)
        ZOOM_resultset_option_set(m_resultset, name, value);
}

void yf::Zoom::Backend::set_option(const char *name, std::string value)
{
    set_option(name, value.c_str());
}

const char *yf::Zoom::Backend::get_option(const char *name)
{
    return ZOOM_connection_option_get(m_connection, name);
}

yf::Zoom::Searchable::Searchable(CCL_bibset base)
{
    piggyback = true;
    use_turbomarc = true;
    sortStrategy = "embed";
    retry_on_failure = "1"; // existing default (should have been false)
    ccl_bibset = ccl_qual_dup(base);
}

yf::Zoom::Searchable::~Searchable()
{
    ccl_qual_rm(&ccl_bibset);
}

yf::Zoom::Frontend::Frontend(Impl *impl) :
    m_p(impl), m_is_virtual(false), m_in_use(true)
{
}

yf::Zoom::Frontend::~Frontend()
{
}

yf::Zoom::FrontendPtr yf::Zoom::Impl::get_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);

    std::map<mp::Session,yf::Zoom::FrontendPtr>::iterator it;

    while(true)
    {
        it = m_clients.find(package.session());
        if (it == m_clients.end())
            break;

        if (!it->second->m_in_use)
        {
            it->second->m_in_use = true;
            return it->second;
        }
        m_cond_session_ready.wait(lock);
    }
    FrontendPtr f(new Frontend(this));
    m_clients[package.session()] = f;
    f->m_in_use = true;
    return f;
}

void yf::Zoom::Impl::release_frontend(mp::Package &package)
{
    boost::mutex::scoped_lock lock(m_mutex);
    std::map<mp::Session,yf::Zoom::FrontendPtr>::iterator it;

    it = m_clients.find(package.session());
    if (it != m_clients.end())
    {
        if (package.session().is_closed())
        {
            m_clients.erase(it);
        }
        else
        {
            it->second->m_in_use = false;
        }
        m_cond_session_ready.notify_all();
    }
}

yf::Zoom::Impl::Impl() :
    apdu_log(false), element_transform("pz2") , element_raw("raw") ,
    element_passthru("F"),
    zoom_timeout("40"), proxy_timeout(1)
{
    bibset = ccl_qual_mk();

    explain_xsp = 0;
    record_xsp = 0;
    srand((unsigned int) time(0));
}

yf::Zoom::Impl::~Impl()
{
    if (explain_xsp)
        xsltFreeStylesheet(explain_xsp);
    ccl_qual_rm(&bibset);
}

yf::Zoom::SearchablePtr yf::Zoom::Impl::parse_torus_record(const xmlNode *ptr)
{
    Zoom::SearchablePtr s(new Searchable(bibset));

    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "layer"))
            ptr = ptr->children;
        else if (!strcmp((const char *) ptr->name,
                         "authentication"))
        {
            s->authentication = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "authenticationMode"))
        {
            s->authenticationMode = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "contentAuthentication"))
        {
            s->contentAuthentication = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "cfAuth"))
        {
            s->cfAuth = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "cfProxy"))
        {
            s->cfProxy = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "cfSubDB"))
        {
            s->cfSubDB = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "contentConnector"))
        {
            s->contentConnector = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "udb"))
        {
            s->udb = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "zurl"))
        {
            s->target = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "sru"))
        {
            s->sru = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "SRUVersion") ||
                 !strcmp((const char *) ptr->name, "sruVersion"))
        {
            s->sru_version = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "queryEncoding"))
        {
            s->query_encoding = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "piggyback"))
        {
            s->piggyback = mp::xml::get_bool(ptr, true);
        }
        else if (!strcmp((const char *) ptr->name,
                         "requestSyntax"))
        {
            s->request_syntax = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "elementSet"))
        {
            s->element_set = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "recordEncoding"))
        {
            s->record_encoding = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "transform"))
        {
            s->transform_xsl_fname = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "literalTransform"))
        {
            s->transform_xsl_content = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "urlRecipe"))
        {
            s->urlRecipe = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                         "useTurboMarc"))
        {
            ; // useTurboMarc is ignored
        }
        else if (!strncmp((const char *) ptr->name,
                          "cclmap_", 7))
        {
            std::string value = mp::xml::get_text(ptr);
            if (value.length() > 0)
            {
                ccl_qual_fitem(s->ccl_bibset, value.c_str(),
                               (const char *) ptr->name + 7);
            }
        }
        else if (!strncmp((const char *) ptr->name,
                          "sortmap_", 8))
        {
            std::string value = mp::xml::get_text(ptr);
            s->sortmap[(const char *) ptr->name + 8] = value;
        }
        else if (!strcmp((const char *) ptr->name,
                          "sortStrategy"))
        {
            s->sortStrategy = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name,
                          "extraArgs"))
        {
            s->extraArgs = mp::xml::get_text(ptr);
        }
        else if (!strcmp((const char *) ptr->name, "rpn2cql"))
            s->rpn2cql_fname = mp::xml::get_text(ptr);
        else if (!strcmp((const char *) ptr->name,
                          "retryOnFailure"))
        {
            s->retry_on_failure = mp::xml::get_text(ptr);
        }
        else if (strlen((const char *) ptr->name) > 3 &&
                 !memcmp((const char *) ptr->name, "cf_", 3))
        {
            s->cf_param[(const char *) ptr->name + 3] = mp::xml::get_text(ptr);
        }
    }
    return s;
}

void yf::Zoom::Impl::configure_local_records(const xmlNode *ptr, bool test_only)
{
    while (ptr && ptr->type != XML_ELEMENT_NODE)
        ptr = ptr->next;

    if (ptr)
    {
        if (!strcmp((const char *) ptr->name, "records"))
        {
            for (ptr = ptr->children; ptr; ptr = ptr->next)
            {
                if (ptr->type != XML_ELEMENT_NODE)
                    continue;
                if (!strcmp((const char *) ptr->name, "record"))
                {
                    SearchablePtr s = parse_torus_record(ptr);
                    if (s)
                    {
                        std::string udb = s->udb;
                        if (udb.length())
                            s_map[s->udb] = s;
                        else
                        {
                            throw mp::filter::FilterException
                                ("No udb for local torus record");
                        }
                    }
                }
                else
                {
                    throw mp::filter::FilterException
                        ("Bad element "
                         + std::string((const char *) ptr->name)
                         + " in zoom filter inside element "
                         "<torus><records>");
                }
            }
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element "
                 + std::string((const char *) ptr->name)
                 + " in zoom filter inside element <torus>");
        }
    }
}

void yf::Zoom::Impl::configure(const xmlNode *ptr, bool test_only,
                               const char *path)
{
    std::string explain_xslt_fname;
    std::string record_xslt_fname;

    if (path && *path)
    {
        file_path = path;
    }
    for (ptr = ptr->children; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        else if (!strcmp((const char *) ptr->name, "torus"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "url"))
                    torus_searchable_url = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "content_url"))
                    torus_content_url = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "auth_url"))
                    torus_auth_url = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "allow_ip"))
                    torus_allow_ip = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "realm"))
                    default_realm = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "auth_hostname"))
                    torus_auth_hostname = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "xsldir"))
                    xsldir = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "element_transform"))
                    element_transform = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "element_raw"))
                    element_raw = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "element_passthru"))
                    element_passthru = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "proxy"))
                    proxy = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "explain_xsl"))
                    explain_xslt_fname = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "record_xsl"))
                    record_xslt_fname = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
            // If content_url is not given, use value of searchable, to
            // ensure backwards compatibility
            if (!torus_content_url.length())
                torus_content_url = torus_searchable_url;
            configure_local_records(ptr->children, test_only);
        }
        else if (!strcmp((const char *) ptr->name, "cclmap"))
        {
            const char *addinfo = 0;
            ccl_xml_config(bibset, ptr, &addinfo);
        }
        else if (!strcmp((const char *) ptr->name, "fieldmap"))
        {
            const struct _xmlAttr *attr;
            std::string ccl_field;
            std::string cql_field;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "ccl"))
                    ccl_field = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "cql"))
                    cql_field = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
            if (cql_field.length())
                fieldmap[cql_field] = ccl_field;
        }
        else if (!strcmp((const char *) ptr->name, "contentProxy"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "server"))
                {
                    yaz_log(YLOG_WARN,
                            "contentProxy's server attribute is deprecated");
                    yaz_log(YLOG_LOG,
                            "Specify config_file instead. For example:");
                    yaz_log(YLOG_LOG,
                            " content_file=\"/etc/cf-proxy/cproxy.cfg\"");
                    content_proxy_server = mp::xml::get_text(attr->children);
                }
                else if (!strcmp((const char *) attr->name, "tmp_file"))
                    content_tmp_file = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "config_file"))
                    content_config_file = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else if (!strcmp((const char *) ptr->name, "log"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "apdu"))
                    apdu_log = mp::xml::get_bool(attr->children, false);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else if (!strcmp((const char *) ptr->name, "zoom"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "timeout"))
                    zoom_timeout = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "proxy_timeout"))
                    proxy_timeout = mp::xml::get_int(attr->children, 1);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element "
                 + std::string((const char *) ptr->name)
                 + " in zoom filter");
        }
    }

    if (explain_xslt_fname.length())
    {
        const char *path = 0;

        if (xsldir.length())
            path = xsldir.c_str();
        else
            path = file_path.c_str();

        char fullpath[1024];
        char *cp = yaz_filepath_resolve(explain_xslt_fname.c_str(),
                                        path, 0, fullpath);
        if (!cp)
        {
            throw mp::filter::FilterException
                ("Cannot read XSLT " + explain_xslt_fname);
        }

        xmlDoc *xsp_doc = xmlParseFile(cp);
        if (!xsp_doc)
        {
            throw mp::filter::FilterException
                ("Cannot parse XSLT " + explain_xslt_fname);
        }

        explain_xsp = xsltParseStylesheetDoc(xsp_doc);
        if (!explain_xsp)
        {
            xmlFreeDoc(xsp_doc);
            throw mp::filter::FilterException
                ("Cannot parse XSLT " + explain_xslt_fname);

        }
    }

    if (record_xslt_fname.length())
    {
        const char *path = 0;

        if (xsldir.length())
            path = xsldir.c_str();
        else
            path = file_path.c_str();

        char fullpath[1024];
        char *cp = yaz_filepath_resolve(record_xslt_fname.c_str(),
                                        path, 0, fullpath);
        if (!cp)
        {
            throw mp::filter::FilterException
                ("Cannot read XSLT " + record_xslt_fname);
        }

        xmlDoc *xsp_doc = xmlParseFile(cp);
        if (!xsp_doc)
        {
            throw mp::filter::FilterException
                ("Cannot parse XSLT " + record_xslt_fname);
        }

        record_xsp = xsltParseStylesheetDoc(xsp_doc);
        if (!record_xsp)
        {
            xmlFreeDoc(xsp_doc);
            throw mp::filter::FilterException
                ("Cannot parse XSLT " + record_xslt_fname);

        }
    }
}

bool yf::Zoom::Frontend::create_content_session(mp::Package &package,
                                                BackendPtr b,
                                                int *error, char **addinfo,
                                                ODR odr,
                                                std::string authentication,
                                                std::string proxy,
                                                std::string realm)
{
    if (b->sptr->contentConnector.length())
    {
        std::string proxyhostname;
        std::string tmp_file;
        bool legacy_format = false;

        if (m_p->content_proxy_server.length())
        {
            proxyhostname = m_p->content_proxy_server;
            legacy_format = true;
        }

        if (m_p->content_tmp_file.length())
            tmp_file = m_p->content_tmp_file;

        if (m_p->content_config_file.length())
        {
            FILE *inf = fopen(m_p->content_config_file.c_str(), "r");
            if (inf)
            {
                char buf[1024];
                while (fgets(buf, sizeof(buf)-1, inf))
                {
                    char *cp;
                    cp = strchr(buf, '#');
                    if (cp)
                        *cp = '\0';
                    cp = strchr(buf, '\n');
                    if (cp)
                        *cp = '\0';
                    cp = strchr(buf, ':');
                    if (cp)
                    {
                        char *cp1 = cp;
                        while (cp1 != buf && cp1[-1] == ' ')
                            cp1--;
                        *cp1 = '\0';
                        cp++;
                        while (*cp == ' ')
                            cp++;
                        if (!strcmp(buf, "proxyhostname"))
                            proxyhostname = cp;
                        if (!strcmp(buf, "sessiondir") && *cp)
                        {
                            if (cp[strlen(cp)-1] == '/')
                                cp[strlen(cp)-1] = '\0';
                            tmp_file = std::string(cp) + std::string("/cf.XXXXXX.p");
                        }
                    }
                }
                fclose(inf);
            }
            else
            {
                package.log("zoom", YLOG_WARN|YLOG_ERRNO,
                            "unable to open content config %s",
                            m_p->content_config_file.c_str());
                *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
                *addinfo = (char *)  odr_malloc(odr, 70 + tmp_file.length());
                sprintf(*addinfo, "zoom: unable to open content config %s",
                        m_p->content_config_file.c_str());
                return false;
            }
        }

        if (proxyhostname.length() == 0)
        {
            package.log("zoom", YLOG_WARN, "no proxyhostname");
            return true;
        }
        if (tmp_file.length() == 0)
        {
            package.log("zoom", YLOG_WARN, "no tmp_file");
            return true;
        }

        char *fname = xstrdup(tmp_file.c_str());
        char *xx = strstr(fname, "XXXXXX");
        if (!xx)
        {
            package.log("zoom", YLOG_WARN, "bad tmp_file %s", tmp_file.c_str());
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *)  odr_malloc(odr, 60 + tmp_file.length());
            sprintf(*addinfo, "zoom: bad format of content tmp_file: %s",
                    tmp_file.c_str());
            xfree(fname);
            return false;
        }
        char tmp_char = xx[6];
        sprintf(xx, "%06d", ((unsigned) rand()) % 1000000);
        if (legacy_format)
            b->cproxy_host = std::string(xx) + "." + proxyhostname;
        else
            b->cproxy_host = proxyhostname + "/" + xx;
        xx[6] = tmp_char;

        FILE *file = fopen(fname, "w");
        if (!file)
        {
            package.log("zoom", YLOG_WARN|YLOG_ERRNO, "create %s", fname);
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *) odr_malloc(odr, 50 + strlen(fname));
            sprintf(*addinfo, "zoom: could not create %s", fname);
            xfree(fname);
            return false;
        }
        mp::wrbuf w;
        wrbuf_puts(w, "#content_proxy\n");
        wrbuf_printf(w, "connector: %s\n", b->sptr->contentConnector.c_str());
        if (authentication.length())
            wrbuf_printf(w, "auth: %s\n", authentication.c_str());
        if (proxy.length())
            wrbuf_printf(w, "proxy: %s\n", proxy.c_str());
        if (realm.length())
            wrbuf_printf(w, "realm: %s\n", realm.c_str());

        fwrite(w.buf(), 1, w.len(), file);
        fclose(file);
        package.log("zoom", YLOG_LOG, "content file: %s", fname);
        xfree(fname);
    }
    return true;
}

yf::Zoom::BackendPtr yf::Zoom::Frontend::get_backend_from_databases(
    mp::Package &package,
    std::string &database, int *error, char **addinfo, mp::odr &odr,
    int *proxy_step)
{
    bool connection_reuse = false;
    std::string proxy;

    std::list<BackendPtr>::const_iterator map_it;
    if (m_backend && !m_backend->enable_explain &&
        m_backend->m_frontend_database == database)
    {
        connection_reuse = true;
        proxy = m_backend->m_proxy;
    }

    std::string input_args;
    std::string torus_db;
    size_t db_arg_pos = database.find(',');
    if (db_arg_pos != std::string::npos)
    {
        torus_db = database.substr(0, db_arg_pos);
        input_args = database.substr(db_arg_pos + 1);
    }
    else
        torus_db = database;

    std::string content_proxy;
    std::string realm = session_realm;
    if (realm.length() == 0)
        realm = m_p->default_realm;

    const char *param_user = 0;
    const char *param_password = 0;
    const char *param_content_user = 0;
    const char *param_content_password = 0;
    const char *param_nocproxy = 0;
    const char *param_retry = 0;
    int no_parms = 0;

    char **names;
    char **values;
    int no_out_args = 0;
    if (input_args.length())
        no_parms = yaz_uri_to_array(input_args.c_str(),
                                    odr, &names, &values);
    // adding 20 because we'll be adding other URL args
    const char **out_names = (const char **)
        odr_malloc(odr, (20 + no_parms) * sizeof(*out_names));
    const char **out_values = (const char **)
        odr_malloc(odr, (20 + no_parms) * sizeof(*out_values));

    // may be changed if it's a content connection
    std::string torus_url = m_p->torus_searchable_url;
    int i;
    for (i = 0; i < no_parms; i++)
    {
        const char *name = names[i];
        const char *value = values[i];
        assert(name);
        assert(value);
        if (!strcmp(name, "user"))
            param_user = value;
        else if (!strcmp(name, "password"))
            param_password = value;
        else if (!strcmp(name, "content-user"))
            param_content_user = value;
        else if (!strcmp(name, "content-password"))
            param_content_password = value;
        else if (!strcmp(name, "content-proxy"))
            content_proxy = value;
        else if (!strcmp(name, "nocproxy"))
            param_nocproxy = value;
        else if (!strcmp(name, "retry"))
            param_retry = value;
        else if (!strcmp(name, "proxy"))
        {
            char **dstr;
            int dnum = 0;
            nmem_strsplit(((ODR) odr)->mem, ",", value, &dstr, &dnum);
            if (connection_reuse)
            {
                // find the step after our current proxy
                int i;
                for (i = 0; i < dnum; i++)
                    if (!strcmp(proxy.c_str(), dstr[i]))
                        break;
                if (i >= dnum - 1)
                    *proxy_step = 0;
                else
                    *proxy_step = i + 1;
            }
            else
            {
                // step is known.. Guess our proxy from it
                if (*proxy_step >= dnum)
                    *proxy_step = 0;
                else
                {
                    proxy = dstr[*proxy_step];

                    (*proxy_step)++;
                    if (*proxy_step == dnum)
                        *proxy_step = 0;
                }
            }
        }
        else if (!strcmp(name, "cproxysession"))
        {
            out_names[no_out_args] = name;
            out_values[no_out_args++] = value;
            torus_url = m_p->torus_content_url;
        }
        else if (!strcmp(name, "realm") && session_realm.length() == 0)
            realm = value;
        else if (!strcmp(name, "torus_url") && session_realm.length() == 0)
            torus_url = value;
        else if (name[0] == 'x' && name[1] == '-')
        {
            out_names[no_out_args] = name;
            out_values[no_out_args++] = value;
        }
        else
        {
            BackendPtr notfound;
            char *msg = (char*) odr_malloc(odr, strlen(name) + 30);
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            sprintf(msg, "zoom: bad database argument: %s", name);
            *addinfo = msg;
            return notfound;
        }
    }
    if (proxy.length())
        package.log("zoom", YLOG_LOG, "proxy: %s", proxy.c_str());

    if (connection_reuse)
    {
        m_backend->connect("", error, addinfo, odr);
        return m_backend;
    }

    if (torus_db.compare("IR-Explain---1") == 0)
        return explain_search(package, database, error, addinfo, odr, torus_url,
                              torus_db, realm);

    SearchablePtr sptr;

    std::map<std::string,SearchablePtr>::iterator it;
    it = m_p->s_map.find(torus_db);
    if (it != m_p->s_map.end())
        sptr = it->second;
    else if (torus_url.length() > 0)
    {
        std::string torus_addinfo;
        std::string torus_query = "udb==" + torus_db;
        xmlDoc *doc = mp::get_searchable(package,torus_url, torus_db,
                                         torus_query,
                                         realm, m_p->proxy,
                                         torus_addinfo);
        if (!doc)
        {
            *error = YAZ_BIB1_UNSPECIFIED_ERROR;
            if (torus_addinfo.length())
                *addinfo = odr_strdup(odr, torus_addinfo.c_str());
            BackendPtr b;
            return b;
        }
        const xmlNode *ptr = xmlDocGetRootElement(doc);
        if (ptr && ptr->type == XML_ELEMENT_NODE)
        {
            if (!strcmp((const char *) ptr->name, "record"))
            {
                sptr = m_p->parse_torus_record(ptr);
            }
            else if (!strcmp((const char *) ptr->name, "records"))
            {
                for (ptr = ptr->children; ptr; ptr = ptr->next)
                {
                    if (ptr->type == XML_ELEMENT_NODE
                        && !strcmp((const char *) ptr->name, "record"))
                    {
                        if (sptr)
                        {
                            *error = YAZ_BIB1_UNSPECIFIED_ERROR;
                            *addinfo = (char*)
                                odr_malloc(odr, 40 + torus_db.length());
                            sprintf(*addinfo, "multiple records for udb=%s",
                                    database.c_str());
                            xmlFreeDoc(doc);
                            BackendPtr b;
                            return b;
                        }
                        sptr = m_p->parse_torus_record(ptr);
                    }
                }
            }
            else
            {
                *error = YAZ_BIB1_UNSPECIFIED_ERROR;
                *addinfo = (char*) odr_malloc(
                    odr, 40 + strlen((const char *) ptr->name));
                sprintf(*addinfo, "bad root element for torus: %s", ptr->name);
                xmlFreeDoc(doc);
                BackendPtr b;
                return b;
            }
        }
        xmlFreeDoc(doc);
    }

    if (!sptr)
    {
        *error = YAZ_BIB1_DATABASE_DOES_NOT_EXIST;
        *addinfo = odr_strdup(odr, torus_db.c_str());
        BackendPtr b;
        return b;
    }

    xsltStylesheetPtr xsp = 0;
    if (sptr->transform_xsl_content.length())
    {
        xmlDoc *xsp_doc = xmlParseMemory(sptr->transform_xsl_content.c_str(),
                                         sptr->transform_xsl_content.length());
        if (!xsp_doc)
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = odr_strdup(odr, "zoom: xmlParseMemory failed "
                                  "for literalTransform XSL");
            BackendPtr b;
            return b;
        }
        xsp = xsltParseStylesheetDoc(xsp_doc);
        if (!xsp)
        {
            *error = YAZ_BIB1_DATABASE_DOES_NOT_EXIST;
            *addinfo =
                odr_strdup(odr,"zoom: xsltParseStylesheetDoc failed "
                           "for literalTransform XSL");
            BackendPtr b;
            xmlFreeDoc(xsp_doc);
            return b;
        }
    }
    else if (sptr->transform_xsl_fname.length())
    {
        const char *path = 0;

        if (m_p->xsldir.length())
            path = m_p->xsldir.c_str();
        else
            path = m_p->file_path.c_str();
        std::string fname;

        char fullpath[1024];
        char *cp = yaz_filepath_resolve(sptr->transform_xsl_fname.c_str(),
                                        path, 0, fullpath);
        if (cp)
            fname.assign(cp);
        else
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *)
                odr_malloc(odr, 40 + sptr->transform_xsl_fname.length());
            sprintf(*addinfo, "zoom: could not open file %s",
                    sptr->transform_xsl_fname.c_str());
            BackendPtr b;
            return b;
        }
        xmlDoc *xsp_doc = xmlParseFile(fname.c_str());
        if (!xsp_doc)
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *) odr_malloc(odr, 50 + fname.length());
            sprintf(*addinfo, "zoom: xmlParseFile failed for file %s",
                    fname.c_str());
            BackendPtr b;
            return b;
        }
        xsp = xsltParseStylesheetDoc(xsp_doc);
        if (!xsp)
        {
            *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
            *addinfo = (char *) odr_malloc(odr, 50 + fname.length());
            sprintf(*addinfo, "zoom: xsltParseStylesheetDoc failed "
                    "for file %s", fname.c_str());
            BackendPtr b;
            xmlFreeDoc(xsp_doc);
            return b;
        }
    }

    cql_transform_t cqlt = 0;
    if (sptr->rpn2cql_fname.length())
    {
        char fullpath[1024];
        char *cp = yaz_filepath_resolve(sptr->rpn2cql_fname.c_str(),
                                        m_p->file_path.c_str(), 0, fullpath);
        if (cp)
            cqlt = cql_transform_open_fname(fullpath);
    }
    else
        cqlt = cql_transform_create();

    if (!cqlt)
    {
        *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
        *addinfo = odr_strdup(odr, "zoom: missing/invalid cql2rpn file");
        BackendPtr b;
        xsltFreeStylesheet(xsp);
        return b;
    }

    m_backend.reset();

    BackendPtr b(new Backend);

    b->cqlt = cqlt;
    b->sptr = sptr;
    b->xsp = xsp;
    b->m_frontend_database = database;
    b->enable_cproxy = param_nocproxy ? false : true;

    if (param_retry)
        b->retry_on_failure = param_retry;
    else
        b->retry_on_failure = b->sptr->retry_on_failure;

    if (sptr->query_encoding.length())
        b->set_option("rpnCharset", sptr->query_encoding);

    std::string extraArgs = sptr->extraArgs;

    b->set_option("timeout", m_p->zoom_timeout.c_str());

    if (m_p->apdu_log)
        b->set_option("apdulog", "1");

    if (sptr->piggyback && sptr->sru.length())
        b->set_option("count", "1"); /* some SRU servers INSIST on getting
                                        maximumRecords > 0 */
    b->set_option("piggyback", sptr->piggyback ? "1" : "0");

    std::string authentication = sptr->authentication;
    if (param_user)
    {
        authentication = std::string(param_user);
        if (param_password)
            authentication += "/" + std::string(param_password);
    }
    std::string content_authentication = sptr->contentAuthentication;
    if (param_content_user)
    {
        content_authentication = std::string(param_content_user);
        if (param_content_password)
            content_authentication += "/" + std::string(param_content_password);
    }

    if (proxy.length() == 0)
        proxy = sptr->cfProxy;
    b->m_proxy = proxy;

    if (sptr->cfAuth.length())
    {
        // A CF target
        b->set_option("user", sptr->cfAuth);
        if (param_user)
        {
            out_names[no_out_args] = "user";
            out_values[no_out_args++] = odr_strdup(odr, param_user);
            if (param_password)
            {
                out_names[no_out_args] = "password";
                out_values[no_out_args++] = odr_strdup(odr, param_password);
            }
        }
        else if (authentication.length())
        {
            size_t found = authentication.find('/');
            if (found != std::string::npos)
            {
                out_names[no_out_args] = "user";
                out_values[no_out_args++] =
                    odr_strdup(odr, authentication.substr(0, found).c_str());

                out_names[no_out_args] = "password";
                out_values[no_out_args++] =
                    odr_strdup(odr, authentication.substr(found+1).c_str());
            }
            else
            {
                out_names[no_out_args] = "user";
                out_values[no_out_args++] =
                    odr_strdup(odr, authentication.c_str());
            }
        }
        if (proxy.length())
        {
            out_names[no_out_args] = "proxy";
            out_values[no_out_args++] = odr_strdup(odr, proxy.c_str());
        }
        if (sptr->cfSubDB.length())
        {
            out_names[no_out_args] = "subdatabase";
            out_values[no_out_args++] = odr_strdup(odr, sptr->cfSubDB.c_str());
        }
        if (!param_nocproxy && b->sptr->contentConnector.length())
            param_nocproxy = "1";

        if (param_nocproxy)
        {
            out_names[no_out_args] = "nocproxy";
            out_values[no_out_args++] = odr_strdup(odr, param_nocproxy);
        }
        std::map<std::string,std::string>::const_iterator it;
        for (it = sptr->cf_param.begin(); it != sptr->cf_param.end(); it++)
        {
            int i;
            const char *n = it->first.c_str();
            for (i = 0; i < no_out_args; i++)
                if (!strcmp(n, out_names[i]))
                    break;
            if (i == no_out_args)
            {
                out_names[no_out_args] = odr_strdup(odr, n);
                out_values[no_out_args++] = odr_strdup(odr, it->second.c_str());
            }
        }
    }
    else
    {
        const char *auth = authentication.c_str();
        const char *cp1 = strchr(auth, ' ');
        if (!cp1 && sptr->sru.length())
            cp1 =  strchr(auth, '/');
        if (!cp1)
        {
            /* Z39.50 user/password style, or no password for SRU */
            b->set_option("user", auth);
        }
        else
        {
            /* now consider group as well */
            const char *cp2 = strchr(cp1 + 1, ' ');

            b->set_option("user", auth, cp1 - auth);
            if (!cp2)
                b->set_option("password", cp1 + 1);
            else
            {
                b->set_option("group", cp1 + 1, cp2 - cp1 - 1);
                b->set_option("password", cp2 + 1);
            }
        }
        if (sptr->authenticationMode.length())
            b->set_option("authenticationMode", sptr->authenticationMode);
        if (proxy.length())
            b->set_option("proxy", proxy);
    }
    if (extraArgs.length())
        b->set_option("extraArgs", extraArgs);

    std::string url(sptr->target);
    if (sptr->sru.length())
    {
        b->set_option("sru", sptr->sru);
        if (url.find("://") == std::string::npos)
            url = "http://" + url;
        if (sptr->sru_version.length())
            b->set_option("sru_version", sptr->sru_version);
    }
    if (no_out_args)
    {
        char *x_args = 0;
        out_names[no_out_args] = 0; // terminate list

        yaz_array_to_uri(&x_args, odr, (char **) out_names,
                         (char **) out_values);
        url += "," + std::string(x_args);
    }
    package.log("zoom", YLOG_LOG, "url: %s", url.c_str());
    b->connect(url, error, addinfo, odr);
    if (*error == 0 && b->enable_cproxy)
        create_content_session(package, b, error, addinfo, odr,
                               content_authentication.length() ?
                               content_authentication : authentication,
                               content_proxy.length() ? content_proxy : proxy,
                               realm);
    if (*error == 0)
        m_backend = b;
    return b;
}

void yf::Zoom::Frontend::prepare_elements(BackendPtr b,
                                          Odr_oid *preferredRecordSyntax,
                                          const char *element_set_name,
                                          bool &enable_pz2_retrieval,
                                          bool &enable_pz2_transform,
                                          bool &enable_record_transform,
                                          bool &assume_marc8_charset)
{
    char oid_name_str[OID_STR_MAX];
    const char *syntax_name = 0;

    if (preferredRecordSyntax &&
        !oid_oidcmp(preferredRecordSyntax, yaz_oid_recsyn_xml))
    {
        if (element_set_name &&
            !strcmp(element_set_name, m_p->element_transform.c_str()))
        {
            enable_pz2_retrieval = true;
            enable_pz2_transform = true;
        }
        else if (element_set_name &&
                 !strcmp(element_set_name, m_p->element_raw.c_str()))
        {
            enable_pz2_retrieval = true;
        }
        else if (m_p->record_xsp)
        {
            enable_pz2_retrieval = true;
            enable_pz2_transform = true;
            enable_record_transform = true;
        }
    }

    if (enable_pz2_retrieval)
    {
        std::string configured_request_syntax = b->sptr->request_syntax;
        if (configured_request_syntax.length())
        {
            syntax_name = configured_request_syntax.c_str();
            const Odr_oid *syntax_oid =
                yaz_string_to_oid(yaz_oid_std(), CLASS_RECSYN, syntax_name);
            if (!oid_oidcmp(syntax_oid, yaz_oid_recsyn_usmarc)
                || !oid_oidcmp(syntax_oid, yaz_oid_recsyn_opac))
                assume_marc8_charset = true;
        }
    }
    else if (preferredRecordSyntax)
        syntax_name =
            yaz_oid_to_string_buf(preferredRecordSyntax, 0, oid_name_str);

    if (b->sptr->sru.length())
        syntax_name = "XML";

    b->set_option("preferredRecordSyntax", syntax_name);

    if (enable_pz2_retrieval)
    {
        if (element_set_name && !strcmp(element_set_name,
                                        m_p->element_passthru.c_str()))
            ;
        else
        {
            element_set_name = 0;
            if (b->sptr->element_set.length())
                element_set_name = b->sptr->element_set.c_str();
        }
    }

    b->set_option("elementSetName", element_set_name);
    if (b->sptr->sru.length() && element_set_name)
        b->set_option("schema", element_set_name);
}

Z_Records *yf::Zoom::Frontend::get_explain_records(
    mp::Package &package,
    Odr_int start,
    Odr_int number_to_present,
    int *error,
    char **addinfo,
    Odr_int *number_of_records_returned,
    ODR odr,
    BackendPtr b,
    Odr_oid *preferredRecordSyntax,
    const char *element_set_name)
{
    Odr_int i;
    Z_Records *records = 0;

    if (!b->explain_doc)
    {
        return records;
    }
    if (number_to_present > 10000)
        number_to_present = 10000;

    xmlNode *ptr = xmlDocGetRootElement(b->explain_doc);

    Z_NamePlusRecordList *npl = (Z_NamePlusRecordList *)
        odr_malloc(odr, sizeof(*npl));
    npl->records = (Z_NamePlusRecord **)
        odr_malloc(odr, number_to_present * sizeof(*npl->records));

    for (i = 0; i < number_to_present; i++)
    {
        int num = 0;
        xmlNode *res = xml_node_search(ptr, &num, start + i + 1);
        if (!res)
            break;
        xmlBufferPtr xml_buf = xmlBufferCreate();
        xmlNode *tmp_node = xmlCopyNode(res->children, 1);
        xmlNodeDump(xml_buf, tmp_node->doc, tmp_node, 0, 0);

        Z_NamePlusRecord *npr =
            (Z_NamePlusRecord *) odr_malloc(odr, sizeof(*npr));
        npr->databaseName = odr_strdup(odr, b->m_frontend_database.c_str());
        npr->which = Z_NamePlusRecord_databaseRecord;
        npr->u.databaseRecord =
            z_ext_record_xml(odr,
                             (const char *) xml_buf->content, xml_buf->use);
        npl->records[i] = npr;
        xmlFreeNode(tmp_node);
        xmlBufferFree(xml_buf);
    }
    records = (Z_Records*) odr_malloc(odr, sizeof(*records));
    records->which = Z_Records_DBOSD;
    records->u.databaseOrSurDiagnostics = npl;

    npl->num_records = i;
    *number_of_records_returned = i;
    return records;
}


Z_Records *yf::Zoom::Frontend::get_records(mp::Package &package,
                                           Odr_int start,
                                           Odr_int number_to_present,
                                           int *error,
                                           char **addinfo,
                                           Odr_int *number_of_records_returned,
                                           ODR odr,
                                           BackendPtr b,
                                           Odr_oid *preferredRecordSyntax,
                                           const char *element_set_name)
{
    *number_of_records_returned = 0;
    Z_Records *records = 0;
    bool enable_pz2_retrieval = false; // whether target profile is used
    bool enable_pz2_transform = false; // whether XSLT is used as well
    bool assume_marc8_charset = false;
    bool enable_record_transform = false;

    prepare_elements(b, preferredRecordSyntax,
                     element_set_name,
                     enable_pz2_retrieval,
                     enable_pz2_transform,
                     enable_record_transform,
                     assume_marc8_charset);

    package.log("zoom", YLOG_LOG, "pz2_retrieval: %s . pz2_transform: %s",
                enable_pz2_retrieval ? "yes" : "no",
                enable_pz2_transform ? "yes" : "no");

    if (start < 0 || number_to_present <=0)
        return records;

    if (number_to_present > 10000)
        number_to_present = 10000;

    ZOOM_record *recs = (ZOOM_record *)
        odr_malloc(odr, (size_t) number_to_present * sizeof(*recs));

    b->present(start, number_to_present, recs, error, addinfo, odr);

    int i = 0;
    if (!*error)
    {
        for (i = 0; i < number_to_present; i++)
        {
            if (!recs[i])
                break;

            const char *addinfo;
            int sur_error = ZOOM_record_error(recs[i], 0 /* msg */,
                                              &addinfo, 0 /* diagset */);
            if (sur_error ==
                YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS && addinfo &&
                !strcmp(addinfo,
                        "ZOOM C generated. Present phase and no records"))
                break;
        }
    }
    if (i > 0)
    {  // only return records if no error and at least one record

        const char *xsl_parms[3];
        mp::wrbuf cproxy_host;

        if (b->enable_cproxy && b->cproxy_host.length())
        {
            wrbuf_puts(cproxy_host, "\"");
            wrbuf_puts(cproxy_host, b->cproxy_host.c_str());
            wrbuf_puts(cproxy_host, "/\"");

            xsl_parms[0] = "cproxyhost";
            xsl_parms[1] = wrbuf_cstr(cproxy_host);
            xsl_parms[2] = 0;
        }
        else
        {
            xsl_parms[0] = 0;
        }

        char *odr_database = odr_strdup(odr,
                                        b->m_frontend_database.c_str());
        Z_NamePlusRecordList *npl = (Z_NamePlusRecordList *)
            odr_malloc(odr, sizeof(*npl));
        *number_of_records_returned = i;
        npl->num_records = i;
        npl->records = (Z_NamePlusRecord **)
            odr_malloc(odr, i * sizeof(*npl->records));
        for (i = 0; i < npl->num_records; i++)
        {
            Z_NamePlusRecord *npr = 0;
            const char *addinfo;

            int sur_error = ZOOM_record_error(recs[i], 0 /* msg */,
                                              &addinfo, 0 /* diagset */);

            if (sur_error)
            {
                log_diagnostic(package, sur_error, addinfo);
                npr = zget_surrogateDiagRec(odr, odr_database, sur_error,
                                            addinfo);
            }
            else if (enable_pz2_retrieval)
            {
                char rec_type_str[100];
                const char *record_encoding = 0;

                if (b->sptr->record_encoding.length())
                    record_encoding = b->sptr->record_encoding.c_str();
                else if (assume_marc8_charset)
                    record_encoding = "marc8";

                strcpy(rec_type_str, b->sptr->use_turbomarc ? "txml" : "xml");
                if (record_encoding)
                {
                    strcat(rec_type_str, "; charset=");
                    strcat(rec_type_str, record_encoding);
                }

                package.log("zoom", YLOG_LOG, "Getting record of type %s",
                            rec_type_str);
                int rec_len;
                xmlChar *xmlrec_buf = 0;
                const char *rec_buf = ZOOM_record_get(recs[i], rec_type_str,
                                                      &rec_len);
                if (!rec_buf && !npr)
                {
                    std::string addinfo("ZOOM_record_get failed for type ");

                    int error = YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS;
                    addinfo += rec_type_str;
                    log_diagnostic(package, error, addinfo.c_str());
                    npr = zget_surrogateDiagRec(odr, odr_database,
                                                error, addinfo.c_str());
                }
                else
                {
                    package.log_write(rec_buf, rec_len);
                    package.log_write("\r\n", 2);
                }

                if (rec_buf && b->xsp && enable_pz2_transform)
                {
                    xmlDoc *rec_doc = xmlParseMemory(rec_buf, rec_len);
                    if (!rec_doc)
                    {
                        const char *addinfo = "xml parse failed for record";
                        int error = YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS;
                        log_diagnostic(package, error, addinfo);
                        npr = zget_surrogateDiagRec(
                            odr, odr_database, error, addinfo);
                    }
                    else
                    {
                        // first stage XSLT - per target
                        xsltStylesheetPtr xsp = b->xsp;
                        xmlDoc *rec_res = xsltApplyStylesheet(xsp, rec_doc,
                                                              xsl_parms);
                        // insert generated-url
                        if (rec_res)
                        {
                            std::string res =
                                mp::xml::url_recipe_handle(rec_res,
                                                           b->sptr->urlRecipe);
                            if (res.length())
                            {
                                xmlNode *ptr = xmlDocGetRootElement(rec_res);
                                while (ptr && ptr->type != XML_ELEMENT_NODE)
                                    ptr = ptr->next;
                                xmlNode *c =
                                    xmlNewChild(ptr, 0, BAD_CAST "metadata", 0);
                                xmlNewProp(c, BAD_CAST "type", BAD_CAST
                                           "generated-url");
                                xmlNode * t = xmlNewText(BAD_CAST res.c_str());
                                xmlAddChild(c, t);
                            }
                        }
                        // second stage XSLT - common
                        if (rec_res && m_p->record_xsp &&
                            enable_record_transform)
                        {
                            xmlDoc *tmp_doc = rec_res;

                            xsp = m_p->record_xsp;
                            rec_res = xsltApplyStylesheet(xsp, tmp_doc,
                                                          xsl_parms);
                            xmlFreeDoc(tmp_doc);
                        }
                        // get result out of it
                        if (rec_res)
                        {
                            xsltSaveResultToString(&xmlrec_buf, &rec_len,
                                                   rec_res, xsp);
                            rec_buf = (const char *) xmlrec_buf;
                            package.log_write(rec_buf, rec_len);

                            xmlFreeDoc(rec_res);
                        }
                        if (!rec_buf)
                        {
                            std::string addinfo;
                            int error =
                                YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS;

                            addinfo = "xslt apply failed for "
                                + b->sptr->transform_xsl_fname;
                            log_diagnostic(package, error, addinfo.c_str());
                            npr = zget_surrogateDiagRec(
                                odr, odr_database, error, addinfo.c_str());
                        }
                        xmlFreeDoc(rec_doc);
                    }
                }

                if (!npr)
                {
                    if (!rec_buf)
                        npr = zget_surrogateDiagRec(
                            odr, odr_database,
                            YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                            rec_type_str);
                    else
                    {
                        npr = (Z_NamePlusRecord *)
                            odr_malloc(odr, sizeof(*npr));
                        npr->databaseName = odr_database;
                        npr->which = Z_NamePlusRecord_databaseRecord;
                        npr->u.databaseRecord =
                            z_ext_record_xml(odr, rec_buf, rec_len);
                    }
                }
                if (xmlrec_buf)
                    xmlFree(xmlrec_buf);
            }
            else
            {
                Z_External *ext =
                    (Z_External *) ZOOM_record_get(recs[i], "ext", 0);
                if (ext)
                {
                    npr = (Z_NamePlusRecord *) odr_malloc(odr, sizeof(*npr));
                    npr->databaseName = odr_database;
                    npr->which = Z_NamePlusRecord_databaseRecord;
                    npr->u.databaseRecord = ext;
                }
                else
                {
                    npr = zget_surrogateDiagRec(
                        odr, odr_database,
                        YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS,
                        "ZOOM_record, type ext");
                }
            }
            npl->records[i] = npr;
        }
        records = (Z_Records*) odr_malloc(odr, sizeof(*records));
        records->which = Z_Records_DBOSD;
        records->u.databaseOrSurDiagnostics = npl;
    }
    return records;
}

struct cql_node *yf::Zoom::Impl::convert_cql_fields(struct cql_node *cn,
                                                    ODR odr)
{
    struct cql_node *r = 0;
    if (!cn)
        return 0;
    switch (cn->which)
    {
    case CQL_NODE_ST:
        if (cn->u.st.index)
        {
            std::map<std::string,std::string>::const_iterator it;
            it = fieldmap.find(cn->u.st.index);
            if (it == fieldmap.end())
                return cn;
            if (it->second.length())
                cn->u.st.index = odr_strdup(odr, it->second.c_str());
            else
                cn->u.st.index = 0;
        }
        break;
    case CQL_NODE_BOOL:
        r = convert_cql_fields(cn->u.boolean.left, odr);
        if (!r)
            r = convert_cql_fields(cn->u.boolean.right, odr);
        break;
    case CQL_NODE_SORT:
        r = convert_cql_fields(cn->u.sort.search, odr);
        break;
    }
    return r;
}

void yf::Zoom::Frontend::log_diagnostic(mp::Package &package,
                                        int error, const char *addinfo)
{
    const char *err_msg = yaz_diag_bib1_str(error);
    if (addinfo)
        package.log("zoom", YLOG_WARN, "Diagnostic %d %s: %s",
                    error, err_msg, addinfo);
    else
        package.log("zoom", YLOG_WARN, "Diagnostic %d %s:",
                    error, err_msg);
}

yf::Zoom::BackendPtr yf::Zoom::Frontend::explain_search(mp::Package &package,
                                                        std::string &database,
                                                        int *error,
                                                        char **addinfo,
                                                        mp::odr &odr,
                                                        std::string torus_url,
                                                        std::string &torus_db,
                                                        std::string &realm)
{
    m_backend.reset();

    BackendPtr b(new Backend);

    b->m_frontend_database = database;
    b->enable_explain = true;

    Z_GDU *gdu = package.request().get();
    Z_APDU *apdu_req = gdu->u.z3950;
    Z_SearchRequest *sr = apdu_req->u.searchRequest;
    Z_Query *query = sr->query;

    if (!m_p->explain_xsp)
    {
        *error = YAZ_BIB1_UNSPECIFIED_ERROR;
        *addinfo =
            odr_strdup(odr, "IR-Explain---1 unsupported. "
                       "Torus explain_xsl not defined");
        return m_backend;
    }
    else if (query->which == Z_Query_type_104 &&
        query->u.type_104->which == Z_External_CQL)
    {
        std::string torus_addinfo;
        std::string torus_query(query->u.type_104->u.cql);
        xmlDoc *doc = mp::get_searchable(package, torus_url, "",
                                         torus_query,
                                         realm, m_p->proxy,
                                         torus_addinfo);
        if (m_p->explain_xsp)
        {
            xmlDoc *rec_res =  xsltApplyStylesheet(m_p->explain_xsp, doc, 0);

            xmlFreeDoc(doc);
            doc = rec_res;
        }
        if (!doc)
        {
            *error = YAZ_BIB1_UNSPECIFIED_ERROR;
            if (torus_addinfo.length())
                *addinfo = odr_strdup(odr, torus_addinfo.c_str());
        }
        else
        {
            xmlNode *ptr = xmlDocGetRootElement(doc);
            int hits = 0;

            xml_node_search(ptr, &hits, 0);

            Z_APDU *apdu_res = odr.create_searchResponse(apdu_req, 0, 0);
            apdu_res->u.searchResponse->resultCount = odr_intdup(odr, hits);
            package.response() = apdu_res;
            m_backend = b;
        }
        if (b->explain_doc)
            xmlFreeDoc(b->explain_doc);
        b->explain_doc = doc;
        return m_backend;
    }
    else
    {
        *error = YAZ_BIB1_QUERY_TYPE_UNSUPP;
        *addinfo = odr_strdup(odr, "IR-Explain---1 only supports CQL");
        return m_backend;
    }
}

static bool wait_conn(COMSTACK cs, int secs)
{
    struct yaz_poll_fd pfd;

    pfd.input_mask = yaz_poll_none;
    pfd.output_mask = yaz_poll_none;
    yaz_poll_add(pfd.input_mask, yaz_poll_except);
    if (cs->io_pending & CS_WANT_WRITE)
        yaz_poll_add(pfd.input_mask, yaz_poll_write);
    if (cs->io_pending & CS_WANT_READ)
        yaz_poll_add(pfd.input_mask, yaz_poll_read);

    pfd.fd = cs_fileno(cs);
    pfd.client_data = 0;

    int ret = yaz_poll(&pfd, 1, secs, 0);
    return ret > 0;
}

bool yf::Zoom::Impl::check_proxy(const char *proxy)
{
    COMSTACK conn = 0;
    const char *uri = "http://localhost/";
    void *add;
    mp::odr odr;
    bool outcome = false;
    conn = cs_create_host_proxy(uri, 0, &add, proxy);

    if (!conn)
        return false;

    Z_GDU *gdu = z_get_HTTP_Request_uri(odr, uri, 0, 1);
    gdu->u.HTTP_Request->method = odr_strdup(odr, "GET");

    if (z_GDU(odr, &gdu, 0, 0))
    {
        int len;
        char *buf = odr_getbuf(odr, &len, 0);

        int ret = cs_connect(conn, add);
        if (ret > 0 || (ret == 0 && wait_conn(conn, 1)))
        {
            while (1)
            {
                ret = cs_put(conn, buf, len);
                if (ret != 1)
                    break;
                if (!wait_conn(conn, proxy_timeout))
                    break;
            }
            if (ret == 0)
                outcome = true;
        }
    }
    cs_close(conn);
    return outcome;
}

bool yf::Zoom::Frontend::retry(mp::Package &package,
                               mp::odr &odr,
                               BackendPtr b,
                               int &error, char **addinfo,
                               int &proxy_step, int &same_retries,
                               int &proxy_retries)
{
    if (b && b->m_proxy.length() && !m_p->check_proxy(b->m_proxy.c_str()))
    {
        log_diagnostic(package, error, *addinfo);
        package.log("zoom", YLOG_LOG, "proxy %s fails", b->m_proxy.c_str());
        m_backend.reset();
        if (proxy_step) // there is a failover
        {
            proxy_retries++;
            package.log("zoom", YLOG_WARN, "search failed: trying next proxy");
            return true;
        }
        error = YAZ_BIB1_PROXY_FAILURE;
        *addinfo = odr_strdup(odr, b->m_proxy.c_str());
    }
    else if (b && b->retry_on_failure.compare("0")
             && same_retries == 0 && proxy_retries == 0)
    {
        log_diagnostic(package, error, *addinfo);
        same_retries++;
        package.log("zoom", YLOG_WARN, "search failed: retry");
        m_backend.reset();
        proxy_step = 0;
        return true;
    }
    return false;
}

void yf::Zoom::Frontend::handle_search(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    Z_APDU *apdu_req = gdu->u.z3950;
    Z_APDU *apdu_res = 0;
    mp::odr odr;
    Z_SearchRequest *sr = apdu_req->u.searchRequest;
    if (sr->num_databaseNames != 1)
    {
        int error = YAZ_BIB1_TOO_MANY_DATABASES_SPECIFIED;
        log_diagnostic(package, error, 0);
        apdu_res = odr.create_searchResponse(apdu_req, error, 0);
        package.response() = apdu_res;
        return;
    }
    int proxy_step = 0;
    int same_retries = 0;
    int proxy_retries = 0;

next_proxy:

    int error = 0;
    char *addinfo = 0;
    std::string db(sr->databaseNames[0]);

    BackendPtr b = get_backend_from_databases(package, db, &error,
                                              &addinfo, odr, &proxy_step);
    if (error)
    {
        if (retry(package, odr, b, error, &addinfo, proxy_step,
                  same_retries, proxy_retries))
            goto next_proxy;
    }
    if (error)
    {
        log_diagnostic(package, error, addinfo);
        apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
        package.response() = apdu_res;
        return;
    }
    if (!b || b->enable_explain)
        return;

    b->set_option("setname", "default");

    bool enable_pz2_retrieval = false;
    bool enable_pz2_transform = false;
    bool enable_record_transform = false;
    bool assume_marc8_charset = false;
    prepare_elements(b, sr->preferredRecordSyntax, 0 /*element_set_name */,
                     enable_pz2_retrieval,
                     enable_pz2_transform,
                     enable_record_transform,
                     assume_marc8_charset);

    Odr_int hits = 0;
    Z_Query *query = sr->query;
    mp::wrbuf ccl_wrbuf;
    mp::wrbuf pqf_wrbuf;
    std::string sortkeys;

    if (query->which == Z_Query_type_1 || query->which == Z_Query_type_101)
    {
        // RPN
        yaz_rpnquery_to_wrbuf(pqf_wrbuf, query->u.type_1);
    }
    else if (query->which == Z_Query_type_2)
    {
        // CCL
        wrbuf_write(ccl_wrbuf, (const char *) query->u.type_2->buf,
                    query->u.type_2->len);
    }
    else if (query->which == Z_Query_type_104 &&
             query->u.type_104->which == Z_External_CQL)
    {
        // CQL
        const char *cql = query->u.type_104->u.cql;
        CQL_parser cp = cql_parser_create();
        int r = cql_parser_string(cp, cql);
        package.log("zoom", YLOG_LOG, "CQL: %s", cql);
        if (r)
        {
            cql_parser_destroy(cp);
            error = YAZ_BIB1_MALFORMED_QUERY;
            const char *addinfo = "CQL syntax error";
            log_diagnostic(package, error, addinfo);
            apdu_res =
                odr.create_searchResponse(apdu_req, error, addinfo);
            package.response() = apdu_res;
            return;
        }
        struct cql_node *cn = cql_parser_result(cp);
        struct cql_node *cn_error = m_p->convert_cql_fields(cn, odr);
        if (cn_error)
        {
            // hopefully we are getting a ptr to a index+relation+term node
            error = YAZ_BIB1_UNSUPP_USE_ATTRIBUTE;
            addinfo = 0;
            if (cn_error->which == CQL_NODE_ST)
                addinfo = cn_error->u.st.index;

            log_diagnostic(package, error, addinfo);
            apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
            package.response() = apdu_res;
            cql_parser_destroy(cp);
            return;
        }
        r = cql_to_ccl(cn, wrbuf_vp_puts,  ccl_wrbuf);
        if (r)
        {
            error = YAZ_BIB1_MALFORMED_QUERY;
            const char *addinfo = "CQL to CCL conversion error";

            log_diagnostic(package, error, addinfo);
            apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
            package.response() = apdu_res;
            cql_parser_destroy(cp);
            return;
        }

        mp::wrbuf sru_sortkeys_wrbuf;
        if (cql_sortby_to_sortkeys(cn, wrbuf_vp_puts, sru_sortkeys_wrbuf))
        {
            error = YAZ_BIB1_ILLEGAL_SORT_RELATION;
            const char *addinfo = "CQL to CCL sortby conversion";

            log_diagnostic(package, error, addinfo);
            apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
            package.response() = apdu_res;
            cql_parser_destroy(cp);
            return;
        }
        mp::wrbuf sort_spec_wrbuf;
        yaz_srw_sortkeys_to_sort_spec(wrbuf_cstr(sru_sortkeys_wrbuf),
                                      sort_spec_wrbuf);
        yaz_tok_cfg_t tc = yaz_tok_cfg_create();
        yaz_tok_parse_t tp =
            yaz_tok_parse_buf(tc, wrbuf_cstr(sort_spec_wrbuf));
        yaz_tok_cfg_destroy(tc);

        /* go through sortspec and map fields */
        int token = yaz_tok_move(tp);
        while (token != YAZ_TOK_EOF)
        {
            if (token == YAZ_TOK_STRING)
            {
                const char *field = yaz_tok_parse_string(tp);
                std::map<std::string,std::string>::iterator it;
                it = b->sptr->sortmap.find(field);
                if (it != b->sptr->sortmap.end())
                    sortkeys += it->second;
                else
                    sortkeys += field;
            }
            sortkeys += " ";
            token = yaz_tok_move(tp);
            if (token == YAZ_TOK_STRING)
            {
                sortkeys += yaz_tok_parse_string(tp);
            }
            if (token != YAZ_TOK_EOF)
            {
                sortkeys += " ";
                token = yaz_tok_move(tp);
            }
        }
        yaz_tok_parse_destroy(tp);
        cql_parser_destroy(cp);
    }
    else
    {
        error = YAZ_BIB1_QUERY_TYPE_UNSUPP;
        const char *addinfo = 0;
        log_diagnostic(package, error, addinfo);
        apdu_res =  odr.create_searchResponse(apdu_req, error, addinfo);
        package.response() = apdu_res;
        return;
    }

    if (ccl_wrbuf.len())
    {
        // CCL to PQF
        assert(pqf_wrbuf.len() == 0);
        int cerror, cpos;
        struct ccl_rpn_node *cn;
        package.log("zoom", YLOG_LOG, "CCL: %s", wrbuf_cstr(ccl_wrbuf));
        cn = ccl_find_str(b->sptr->ccl_bibset, wrbuf_cstr(ccl_wrbuf),
                          &cerror, &cpos);
        if (!cn)
        {
            char *addinfo = odr_strdup_null(odr, ccl_err_msg(cerror));
            error = YAZ_BIB1_MALFORMED_QUERY;

            switch (cerror)
            {
            case CCL_ERR_UNKNOWN_QUAL:
            case CCL_ERR_TRUNC_NOT_LEFT:
            case CCL_ERR_TRUNC_NOT_RIGHT:
            case CCL_ERR_TRUNC_NOT_BOTH:
            case CCL_ERR_TRUNC_NOT_EMBED:
            case CCL_ERR_TRUNC_NOT_SINGLE:
                error = YAZ_BIB1_UNSUPP_SEARCH;
                break;
            }
            log_diagnostic(package, error, addinfo);
            apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
            package.response() = apdu_res;
            return;
        }
        ccl_pquery(pqf_wrbuf, cn);
        package.log("zoom", YLOG_LOG, "RPN: %s", wrbuf_cstr(pqf_wrbuf));
        ccl_rpn_delete(cn);
    }

    assert(pqf_wrbuf.len());

    ZOOM_query q = ZOOM_query_create();
    ZOOM_query_sortby2(q, b->sptr->sortStrategy.c_str(), sortkeys.c_str());

    Z_FacetList *fl = 0;

    // Facets for request.. And later for reponse
    if (!fl)
        fl = yaz_oi_get_facetlist(&sr->otherInfo);
    if (!fl)
        fl = yaz_oi_get_facetlist(&sr->additionalSearchInfo);

    if (b->get_option("sru"))
    {
        Z_RPNQuery *zquery;
        zquery = p_query_rpn(odr, wrbuf_cstr(pqf_wrbuf));
        mp::wrbuf wrb_cql;
        mp::wrbuf wrb_addinfo;

        if (!strcmp(b->get_option("sru"), "solr"))
            error = solr_transform_rpn2solr_stream_r(b->cqlt, wrb_addinfo,
                                                     wrbuf_vp_puts, wrb_cql,
                                                     zquery);
        else
            error = cql_transform_rpn2cql_stream_r(b->cqlt, wrb_addinfo,
                                                   wrbuf_vp_puts, wrb_cql,
                                                   zquery);
        if (error)
        {
            log_diagnostic(package, error, wrb_addinfo.c_str_null());
            apdu_res = odr.create_searchResponse(apdu_req, error,
                                                 wrb_addinfo.c_str_null());
            package.response() = apdu_res;
            return;
        }
        ZOOM_query_cql(q, wrb_cql.c_str());
        package.log("zoom", YLOG_LOG, "search CQL: %s", wrb_cql.c_str());
        b->search(q, &hits, &error, &addinfo, &fl, odr);
        ZOOM_query_destroy(q);
    }
    else
    {
        ZOOM_query_prefix(q, pqf_wrbuf.c_str());
        package.log("zoom", YLOG_LOG, "search PQF: %s", pqf_wrbuf.c_str());
        b->search(q, &hits, &error, &addinfo, &fl, odr);
        ZOOM_query_destroy(q);
    }

    if (error)
    {
        if (retry(package, odr, b, error, &addinfo, proxy_step,
                  same_retries, proxy_retries))
            goto next_proxy;
    }

    const char *element_set_name = 0;
    Odr_int number_to_present = 0;
    if (!error)
        mp::util::piggyback_sr(sr, hits, number_to_present, &element_set_name);

    Odr_int number_of_records_returned = 0;
    Z_Records *records = get_records(
        package,
        0, number_to_present, &error, &addinfo,
        &number_of_records_returned, odr, b, sr->preferredRecordSyntax,
        element_set_name);
    if (error)
        log_diagnostic(package, error, addinfo);
    apdu_res = odr.create_searchResponse(apdu_req, error, addinfo);
    if (records)
    {
        apdu_res->u.searchResponse->records = records;
        apdu_res->u.searchResponse->numberOfRecordsReturned =
            odr_intdup(odr, number_of_records_returned);
    }
    apdu_res->u.searchResponse->resultCount = odr_intdup(odr, hits);
    if (fl)
        yaz_oi_set_facetlist(&apdu_res->u.searchResponse->additionalSearchInfo,
                             odr, fl);
    package.response() = apdu_res;
}

void yf::Zoom::Frontend::handle_present(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    Z_APDU *apdu_req = gdu->u.z3950;
    Z_APDU *apdu_res = 0;
    Z_PresentRequest *pr = apdu_req->u.presentRequest;

    mp::odr odr;
    if (!m_backend)
    {
        package.response() = odr.create_presentResponse(
            apdu_req, YAZ_BIB1_SPECIFIED_RESULT_SET_DOES_NOT_EXIST, 0);
        return;
    }
    const char *element_set_name = 0;
    Z_RecordComposition *comp = pr->recordComposition;
    if (comp && comp->which != Z_RecordComp_simple)
    {
        package.response() = odr.create_presentResponse(
            apdu_req,
            YAZ_BIB1_PRESENT_COMP_SPEC_PARAMETER_UNSUPP, 0);
        return;
    }
    if (comp && comp->u.simple->which == Z_ElementSetNames_generic)
        element_set_name = comp->u.simple->u.generic;
    Odr_int number_of_records_returned = 0;
    int error = 0;
    char *addinfo = 0;

    if (m_backend->enable_explain)
    {
        Z_Records *records =
            get_explain_records(
                package,
                *pr->resultSetStartPoint - 1, *pr->numberOfRecordsRequested,
                &error, &addinfo, &number_of_records_returned, odr, m_backend,
                pr->preferredRecordSyntax, element_set_name);

        apdu_res = odr.create_presentResponse(apdu_req, error, addinfo);
        if (records)
        {
            apdu_res->u.presentResponse->records = records;
            apdu_res->u.presentResponse->numberOfRecordsReturned =
                odr_intdup(odr, number_of_records_returned);
        }
        package.response() = apdu_res;
    }
    else
    {
        Z_Records *records =
            get_records(package,
                        *pr->resultSetStartPoint - 1, *pr->numberOfRecordsRequested,
                        &error, &addinfo, &number_of_records_returned, odr, m_backend,
                        pr->preferredRecordSyntax, element_set_name);

        apdu_res = odr.create_presentResponse(apdu_req, error, addinfo);
        if (records)
        {
            apdu_res->u.presentResponse->records = records;
            apdu_res->u.presentResponse->numberOfRecordsReturned =
                odr_intdup(odr, number_of_records_returned);
        }
        package.response() = apdu_res;
    }
}

void yf::Zoom::Frontend::handle_package(mp::Package &package)
{
    Z_GDU *gdu = package.request().get();
    if (!gdu)
        ;
    else if (gdu->which == Z_GDU_Z3950)
    {
        Z_APDU *apdu_req = gdu->u.z3950;

        if (m_backend)
            wrbuf_rewind(m_backend->m_apdu_wrbuf);
        if (apdu_req->which == Z_APDU_initRequest)
        {
            mp::odr odr;
            package.response() = odr.create_close(
                apdu_req,
                Z_Close_protocolError,
                "double init");
        }
        else if (apdu_req->which == Z_APDU_searchRequest)
        {
            handle_search(package);
        }
        else if (apdu_req->which == Z_APDU_presentRequest)
        {
            handle_present(package);
        }
        else
        {
            mp::odr odr;
            package.response() = odr.create_close(
                apdu_req,
                Z_Close_protocolError,
                "zoom filter cannot handle this APDU");
            package.session().close();
        }
        if (m_backend)
        {
            WRBUF w = m_backend->m_apdu_wrbuf;
            package.log_write(wrbuf_buf(w), wrbuf_len(w));
        }
    }
    else
    {
        package.session().close();
    }
}

std::string escape_cql_term(std::string inp)
{
    std::string res;
    size_t l = inp.length();
    size_t i;
    for (i = 0; i < l; i++)
    {
        if (strchr("*?^\"", inp[i]))
            res += "\\";
        res += inp[i];
    }
    return res;
}

void yf::Zoom::Frontend::auth(mp::Package &package, Z_InitRequest *req,
                              int *error, char **addinfo, ODR odr)
{
    if (m_p->torus_auth_url.length() == 0)
        return;

    std::string user;
    std::string password;
    if (req->idAuthentication)
    {
        Z_IdAuthentication *auth = req->idAuthentication;
        switch (auth->which)
        {
        case Z_IdAuthentication_open:
            if (auth->u.open)
            {
                const char *cp = strchr(auth->u.open, '/');
                if (cp)
                {
                    user.assign(auth->u.open, cp - auth->u.open);
                    password.assign(cp + 1);
                }
            }
            break;
        case Z_IdAuthentication_idPass:
            if (auth->u.idPass->userId)
                user.assign(auth->u.idPass->userId);
            if (auth->u.idPass->password)
                password.assign(auth->u.idPass->password);
            break;
        }
    }

    Z_OtherInformation **oi = &req->otherInfo;
    const char *ip_cstr =
        yaz_oi_get_string_oid(oi, yaz_oid_userinfo_client_ip, 1, 0);
    std::string ip;
    if (ip_cstr)
        ip = ip_cstr;
    else
        ip = package.origin().get_address();

    yaz_log(YLOG_LOG, "IP=%s", ip.c_str());

    {
        NMEM nmem = nmem_create();
        char **darray;
        int i, num;
        nmem_strsplit_blank(nmem, m_p->torus_allow_ip.c_str(), &darray, &num);
        for (i = 0; i < num; i++)
        {
            yaz_log(YLOG_LOG, "check against %s+%s", darray[i], ip.c_str());
            if (yaz_match_glob(darray[i], ip.c_str()))
                break;
        }
        nmem_destroy(nmem);
        if (i < num)
            return;  /* allow this IP */
    }
    std::string torus_query;
    int failure_code;

    if (user.length() && password.length())
    {
        torus_query = "userName==\"" + escape_cql_term(user) +
            "\" and password==\"" + escape_cql_term(password) + "\"";
        failure_code = YAZ_BIB1_INIT_AC_BAD_USERID_AND_OR_PASSWORD;
    }
    else
    {
        torus_query = "ipRanges encloses/net.ipaddress \"";
        torus_query += escape_cql_term(std::string(ip));
        torus_query += "\"";

        if (m_p->torus_auth_hostname.length())
        {
            torus_query += " AND hostName == \"";
            torus_query += escape_cql_term(m_p->torus_auth_hostname);
            torus_query += "\"";
        }
        failure_code = YAZ_BIB1_INIT_AC_BLOCKED_NETWORK_ADDRESS;
    }

    std::string dummy_db;
    std::string dummy_realm;
    std::string torus_addinfo;
    xmlDoc *doc = mp::get_searchable(package, m_p->torus_auth_url, dummy_db,
                                     torus_query, dummy_realm, m_p->proxy,
                                     torus_addinfo);
    if (!doc)
    {
        // something fundamental broken in lookup.
        *error = YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
        if (torus_addinfo.length())
            *addinfo = odr_strdup(odr, torus_addinfo.c_str());
        return;
    }
    const xmlNode *ptr = xmlDocGetRootElement(doc);
    if (ptr && ptr->type == XML_ELEMENT_NODE)
    {
        if (strcmp((const char *) ptr->name, "records") == 0)
        {
            ptr = ptr->children;
            while (ptr && ptr->type != XML_ELEMENT_NODE)
                ptr = ptr->next;
        }
        if (ptr && strcmp((const char *) ptr->name, "record") == 0)
        {
            ptr = ptr->children;
            while (ptr && ptr->type != XML_ELEMENT_NODE)
                ptr = ptr->next;
        }
        if (ptr && strcmp((const char *) ptr->name, "layer") == 0)
        {
            ptr = ptr->children;
            while (ptr && ptr->type != XML_ELEMENT_NODE)
                ptr = ptr->next;
        }
        while (ptr)
        {
            if (ptr && ptr->type == XML_ELEMENT_NODE &&
                !strcmp((const char *) ptr->name, "identityId"))
                break;
            ptr = ptr->next;
        }
    }
    if (!ptr)
    {
        *error = failure_code;
        return;
    }
    session_realm = mp::xml::get_text(ptr);
}

void yf::Zoom::Impl::process(mp::Package &package)
{
    FrontendPtr f = get_frontend(package);
    Z_GDU *gdu = package.request().get();

    if (f->m_is_virtual)
    {
        f->handle_package(package);
    }
    else if (gdu && gdu->which == Z_GDU_Z3950 && gdu->u.z3950->which ==
             Z_APDU_initRequest)
    {
        Z_InitRequest *req = gdu->u.z3950->u.initRequest;
        f->m_init_gdu = gdu;

        mp::odr odr;
        Z_APDU *apdu = odr.create_initResponse(gdu->u.z3950, 0, 0);
        Z_InitResponse *resp = apdu->u.initResponse;

        int i;
        static const int masks[] = {
            Z_Options_search,
            Z_Options_present,
            -1
        };
        for (i = 0; masks[i] != -1; i++)
            if (ODR_MASK_GET(req->options, masks[i]))
                ODR_MASK_SET(resp->options, masks[i]);

        static const int versions[] = {
            Z_ProtocolVersion_1,
            Z_ProtocolVersion_2,
            Z_ProtocolVersion_3,
            -1
        };
        for (i = 0; versions[i] != -1; i++)
            if (ODR_MASK_GET(req->protocolVersion, versions[i]))
                ODR_MASK_SET(resp->protocolVersion, versions[i]);
            else
                break;

        *resp->preferredMessageSize = *req->preferredMessageSize;
        *resp->maximumRecordSize = *req->maximumRecordSize;

        int error = 0;
        char *addinfo = 0;
        f->auth(package, req, &error, &addinfo, odr);
        if (error)
        {
            resp->userInformationField =
                zget_init_diagnostics(odr, error, addinfo);
            *resp->result = 0;
            package.session().close();
        }
        else
            f->m_is_virtual = true;
        package.response() = apdu;
    }
    else
        package.move();

    release_frontend(package);
}


static mp::filter::Base* filter_creator()
{
    return new mp::filter::Zoom;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_zoom = {
        0,
        "zoom",
        filter_creator
    };
}


/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

