
#include "condor_adtypes.h"
#include "dc_collector.h"

#include <boost/python.hpp>

#include "classad_wrapper.h"

using namespace boost::python;

AdTypes convert_to_ad_type(daemon_t d_type)
{
    AdTypes ad_type = NO_AD;
    switch (d_type)
    {
    case DT_MASTER:
        ad_type = MASTER_AD;
        break;
    case DT_STARTD:
        ad_type = STARTD_AD;
        break;
    case DT_SCHEDD:
        ad_type = SCHEDD_AD;
        break;
    case DT_NEGOTIATOR:
        ad_type = NEGOTIATOR_AD;
        break;
    case DT_COLLECTOR:
        ad_type = COLLECTOR_AD;
        break;
    default:
        PyErr_SetString(PyExc_ValueError, "Unknown daemon type.");
        throw_error_already_set();
    }
    return ad_type;
}

struct Collector {

    Collector(const std::string &pool="")
      : m_collectors(NULL)
    {
        if (pool.size())
            m_collectors = CollectorList::create(pool.c_str());
        else
            m_collectors = CollectorList::create();
    }

    ~Collector()
    {
        if (m_collectors) delete m_collectors;
    }

    object query(AdTypes ad_type, const std::string &constraint, list attrs)
    {
        CondorQuery query(ad_type);
        if (constraint.length())
        {
            query.addANDConstraint(constraint.c_str());
        }
        std::vector<const char *> attrs_char;
        std::vector<std::string> attrs_str;
        if (len(attrs))
        {
            attrs_str.reserve(len(attrs));
            attrs_char.reserve(len(attrs)+1);
            attrs_char[len(attrs)] = NULL;
            for (int i=0; i<len(attrs); i++)
            {
                std::string str = extract<std::string>(attrs[i]);
                attrs_str.push_back(str);
                attrs_char[i] = attrs_str[i].c_str();
            }
            query.setDesiredAttrs(&attrs_char[0]);
        }
        ClassAdList adList;

        QueryResult result = m_collectors->query(query, adList, NULL);

        switch (result)
        {
        case Q_OK:
            break;
        case Q_INVALID_CATEGORY:
            PyErr_SetString(PyExc_RuntimeError, "Category not supported by query type.");
            boost::python::throw_error_already_set();
        case Q_MEMORY_ERROR:
            PyErr_SetString(PyExc_MemoryError, "Memory allocation error.");
            boost::python::throw_error_already_set();
        case Q_PARSE_ERROR:
            PyErr_SetString(PyExc_SyntaxError, "Query constraints could not be parsed.");
            boost::python::throw_error_already_set();
        case Q_COMMUNICATION_ERROR:
            PyErr_SetString(PyExc_IOError, "Failed communication with collector.");
            boost::python::throw_error_already_set();
        case Q_INVALID_QUERY:
            PyErr_SetString(PyExc_RuntimeError, "Invalid query.");
            boost::python::throw_error_already_set();
        case Q_NO_COLLECTOR_HOST:
            PyErr_SetString(PyExc_RuntimeError, "Unable to determine collector host.");
            boost::python::throw_error_already_set();
        default:
            PyErr_SetString(PyExc_RuntimeError, "Unknown error from collector query.");
            boost::python::throw_error_already_set();
        }

        list retval;
        ClassAd * ad;
        adList.Open();
        while ((ad = adList.Next()))
        {
            boost::shared_ptr<ClassAdWrapper> wrapper(new ClassAdWrapper());
            wrapper->CopyFrom(*ad);
            retval.append(wrapper);
        }
        return retval;
    }

    object locateAll(daemon_t d_type)
    {
        AdTypes ad_type = convert_to_ad_type(d_type);
        return query(ad_type, "", list());
    }

    object locate(daemon_t d_type, const std::string &name)
    {
        std::string constraint = ATTR_NAME " =?= \"" + name + "\"";
        object result = query(convert_to_ad_type(d_type), constraint, list());
        if (len(result) >= 1) {
            return result[0];
        }
        PyErr_SetString(PyExc_ValueError, "Unable to find daemon.");
        throw_error_already_set();
        return object();
    }

    ClassAdWrapper *locateLocal(daemon_t d_type)
    {
        Daemon my_daemon( d_type, 0, 0 );

        ClassAdWrapper *wrapper = new ClassAdWrapper();
        if (my_daemon.locate())
        {
            classad::ClassAd *daemonAd;
            if ((daemonAd = my_daemon.daemonAd()))
            {
                wrapper->CopyFrom(*daemonAd);
            }
            else
            {
                std::string addr = my_daemon.addr();
                if (!my_daemon.addr() || !wrapper->InsertAttr(ATTR_SCHEDD_IP_ADDR, addr))
                {
                    PyErr_SetString(PyExc_RuntimeError, "Unable to locate daemon address.");
                    throw_error_already_set();
                }
                std::string name = my_daemon.name() ? my_daemon.name() : "Unknown";
                if (!wrapper->InsertAttr(ATTR_NAME, name))
                {
                    PyErr_SetString(PyExc_RuntimeError, "Unable to insert daemon name.");
                    throw_error_already_set();
                }
                std::string hostname = my_daemon.fullHostname() ? my_daemon.fullHostname() : "Unknown";
                if (!wrapper->InsertAttr(ATTR_MACHINE, hostname))
                {
                    PyErr_SetString(PyExc_RuntimeError, "Unable to insert daemon hostname.");
                    throw_error_already_set();
                }
                std::string version = my_daemon.version() ? my_daemon.version() : "";
                if (!wrapper->InsertAttr(ATTR_VERSION, version))
                {
                    PyErr_SetString(PyExc_RuntimeError, "Unable to insert daemon version.");
                    throw_error_already_set();
                }
            }
        }
        else
        {
            PyErr_SetString(PyExc_RuntimeError, "Unable to locate local daemon");
            boost::python::throw_error_already_set();
        }
        return wrapper;
    }


    // Overloads for the Collector; can't be done in boost.python and provide
    // docstrings.
    object query0()
    {
        return query(ANY_AD, "", list());
    }
    object query1(AdTypes ad_type)
    {
        return query(ad_type, "", list());
    }
    object query2(AdTypes ad_type, const std::string &constraint)
    {
        return query(ad_type, constraint, list());
    }

private:

    CollectorList *m_collectors;

};

void export_collector()
{
    class_<Collector>("Collector", "Client-side operations for the HTCondor collector")
        .def(init<std::string>(":param pool: Name of collector to query; if not specified, uses the local one."))
        .def("query", &Collector::query0)
        .def("query", &Collector::query1)
        .def("query", &Collector::query2)
        .def("query", &Collector::query,
            "Query the contents of a collector.\n"
            ":param ad_type: Type of ad to return from the AdTypes enum; if not specified, uses ANY_AD.\n"
            ":param constraint: A constraint for the ad query; defaults to true.\n"
            ":param attrs: A list of attributes; if specified, the returned ads will be "
            "projected along these attributes.\n"
            ":return: A list of ads in the collector matching the constraint.")
        .def("locate", &Collector::locateLocal, return_value_policy<manage_new_object>())
        .def("locate", &Collector::locate,
            "Query the collector for a particular daemon.\n"
            ":param daemon_type: Type of daemon; must be from the DaemonTypes enum.\n"
            ":param name: Name of daemon to locate.  If not specified, it searches for the local daemon.\n"
            ":return: The ad of the corresponding daemon.")
        .def("locateAll", &Collector::locateAll,
            "Query the collector for all ads of a particular type.\n"
            ":param daemon_type: Type of daemon; must be from the DaemonTypes enum.\n"
            ":return: A list of matching ads.")
        ;
}

