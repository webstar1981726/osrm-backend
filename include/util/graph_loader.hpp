#ifndef GRAPH_LOADER_HPP
#define GRAPH_LOADER_HPP

#include "extractor/external_memory_node.hpp"
#include "extractor/node_based_edge.hpp"
#include "extractor/query_node.hpp"
#include "extractor/restriction.hpp"
#include "util/exception.hpp"
#include "util/fingerprint.hpp"
#include "util/simple_logger.hpp"
#include "util/typedefs.hpp"
#include "storage/io.hpp"

#include <boost/assert.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <tbb/parallel_sort.h>

#include <cmath>

#include <fstream>
#include <ios>
#include <vector>

namespace osrm
{
namespace util
{

/**
 * Reads the .restrictions file and loads it to a vector.
 * The since the restrictions reference nodes using their external node id,
 * we need to renumber it to the new internal id.
*/
inline unsigned loadRestrictionsFromFile(std::string &filename,
                                         std::vector<extractor::TurnRestriction> &restriction_list)
{
    storage::io::FileReader file(filename, storage::io::FileReader::VerifyFingerprint);
    unsigned number_of_usable_restrictions = file.ReadElementCount32();
    
    restriction_list.resize(number_of_usable_restrictions);
    if (number_of_usable_restrictions > 0) {
        file.ReadInto(restriction_list.data(), number_of_usable_restrictions * sizeof(extractor::TurnRestriction));
    }
    
    return number_of_usable_restrictions;
}

/**
 * Reads the beginning of an .osrm file and produces:
 *  - list of barrier nodes
 *  - list of traffic lights
 *  - nodes indexed by their internal (non-osm) id
 */
inline NodeID loadNodesFromFile(std::istream &input_stream,
                                std::vector<NodeID> &barrier_node_list,
                                std::vector<NodeID> &traffic_light_node_list,
                                std::vector<extractor::QueryNode> &node_array)
{
    const FingerPrint fingerprint_valid = FingerPrint::GetValid();
    FingerPrint fingerprint_loaded;
    input_stream.read(reinterpret_cast<char *>(&fingerprint_loaded), sizeof(FingerPrint));

    if (!fingerprint_loaded.TestContractor(fingerprint_valid))
    {
        SimpleLogger().Write(logWARNING) << ".osrm was prepared with different build.\n"
                                            "Reprocess to get rid of this warning.";
    }

    NodeID n;
    input_stream.read(reinterpret_cast<char *>(&n), sizeof(NodeID));
    SimpleLogger().Write() << "Importing n = " << n << " nodes ";

    node_array.reserve(n);

    extractor::ExternalMemoryNode current_node;
    for (NodeID i = 0; i < n; ++i)
    {
        input_stream.read(reinterpret_cast<char *>(&current_node),
                          sizeof(extractor::ExternalMemoryNode));
        node_array.emplace_back(current_node.lon, current_node.lat, current_node.node_id);
        if (current_node.barrier)
        {
            barrier_node_list.emplace_back(i);
        }
        if (current_node.traffic_lights)
        {
            traffic_light_node_list.emplace_back(i);
        }
    }

    // tighten vector sizes
    barrier_node_list.shrink_to_fit();
    traffic_light_node_list.shrink_to_fit();

    return n;
}

/**
 * Reads a .osrm file and produces the edges.
 */
inline NodeID loadEdgesFromFile(std::istream &input_stream,
                                std::vector<extractor::NodeBasedEdge> &edge_list)
{
    EdgeID m;
    input_stream.read(reinterpret_cast<char *>(&m), sizeof(unsigned));
    edge_list.resize(m);
    SimpleLogger().Write() << " and " << m << " edges ";

    input_stream.read((char *)edge_list.data(), m * sizeof(extractor::NodeBasedEdge));

    BOOST_ASSERT(edge_list.size() > 0);

#ifndef NDEBUG
    SimpleLogger().Write() << "Validating loaded edges...";
    tbb::parallel_sort(
        edge_list.begin(),
        edge_list.end(),
        [](const extractor::NodeBasedEdge &lhs, const extractor::NodeBasedEdge &rhs) {
            return (lhs.source < rhs.source) ||
                   (lhs.source == rhs.source && lhs.target < rhs.target);
        });
    for (auto i = 1u; i < edge_list.size(); ++i)
    {
        const auto &edge = edge_list[i];
        const auto &prev_edge = edge_list[i - 1];

        BOOST_ASSERT_MSG(edge.weight > 0, "loaded null weight");
        BOOST_ASSERT_MSG(edge.forward, "edge must be oriented in forward direction");
        BOOST_ASSERT_MSG(edge.travel_mode != TRAVEL_MODE_INACCESSIBLE, "loaded non-accessible");

        BOOST_ASSERT_MSG(edge.source != edge.target, "loaded edges contain a loop");
        BOOST_ASSERT_MSG(edge.source != prev_edge.source || edge.target != prev_edge.target,
                         "loaded edges contain a multi edge");
    }
#endif

    SimpleLogger().Write() << "Graph loaded ok and has " << edge_list.size() << " edges";

    return m;
}
}
}

#endif // GRAPH_LOADER_HPP
