/**
 * NodeRegistry: class that defines a map of key->value where key is a SubDimCell
 *   (e.g. an edge or face bewtween two elements), and value contains various data
 *   about the SubDimCell, such as which element owns the SubDimCell, an array of
 *   new nodes needed on it for refinement, etc.
 */

#ifndef stk_adapt_NodeRegistry_hpp
#define stk_adapt_NodeRegistry_hpp

#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <limits>
#include <cmath>
#include <utility>
#include <math.h>
#include <map>
#include <set>
#include <vector>

#include <Shards_BasicTopologies.hpp>
#include <Shards_CellTopologyData.h>

#include <stk_mesh/base/EntityKey.hpp>

#include <stk_percept/stk_mesh.hpp>

#include <stk_util/environment/CPUTime.hpp>

#include <stk_percept/NoMallocArray.hpp>
#include <stk_percept/PerceptMesh.hpp>
#include <stk_percept/Util.hpp>

#include <stk_percept/PerceptBoostArray.hpp>

#include <boost/tuple/tuple_io.hpp>
#include <boost/tuple/tuple_comparison.hpp>

#define DEBUG_PRINT_11 0
#define NR_PRINT(a) do { if (DEBUG_PRINT_11) std::cout << #a << " = " << a ; } while(0)
#define NR_PRINT_OUT(a,out) do { if (DEBUG_PRINT_11) out << #a << " = " << a << std::endl; } while(0)

/// define only one of these to be 1
/// current best setting is NODE_REGISTRY_MAP_TYPE_BOOST = 1
#define NODE_REGISTRY_MAP_TYPE_BOOST 1

#define STK_ADAPT_NODEREGISTRY_USE_ENTITY_REPO 0
#define STK_ADAPT_NODEREGISTRY_DO_REHASH 1

#if NODE_REGISTRY_MAP_TYPE_BOOST
#include <boost/unordered_map.hpp>
#endif

#define DEBUG_NR_UNREF 0
#define DEBUG_NR_DEEP 0

#include <stk_adapt/SubDimCell.hpp>
#include <stk_adapt/SDCEntityType.hpp>
#include <stk_adapt/NodeIdsOnSubDimEntityType.hpp>

namespace stk {
  namespace adapt {

    using namespace stk::percept;
    using std::vector;
    using std::map;
    using std::set;

    static bool s_allow_empty_sub_dims = true; // for uniform refine, this should be false for testing

    // pair of rank and number of entities of that rank needed on a SubDimCell
    typedef std::pair<stk::mesh::EntityRank, unsigned> NeededEntityType;

    inline std::ostream &operator<<(std::ostream& out, const boost::array<stk::mesh::EntityId, 1>& arr)
    {
      out << arr[0];
      return out;
    }

    typedef boost::array<double, 2> Double2;
    // tuple storage: SDC_DATA_GLOBAL_NODE_IDS, SDC_DATA_OWNING_ELEMENT_KEY,  SDC_DATA_OWNING_ELEMENT_ORDINAL, SDC_DATA_SPACING
    typedef boost::tuple<NodeIdsOnSubDimEntityType, stk::mesh::EntityKey, unsigned char, Double2> SubDimCellData;

    //typedef SubDimCell<SDCEntityType> SubDimCell_SDCEntityType;
    typedef MySubDimCell<SDCEntityType, 4, CompareSDCEntityType> SubDimCell_SDCEntityType;

    inline std::ostream& operator<<(std::ostream& out,  SubDimCellData& val)
    {
      out << "SDC:: node ids= " << val.get<SDC_DATA_GLOBAL_NODE_IDS>()
          << " owning element rank= " << val.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank()
          << " owning element id= " << val.get<SDC_DATA_OWNING_ELEMENT_KEY>().id()
          << " owning element subDim-ord= " << val.get<SDC_DATA_OWNING_ELEMENT_ORDINAL>()
          << " spacing info= " << val.get<SDC_DATA_SPACING>()[0] << " " << val.get<SDC_DATA_SPACING>()[1];
      return out;
    }

  }
}

namespace stk {
  namespace adapt {

    typedef stk::mesh::Entity EntityPtr;

    /// map of the node ids on a sub-dim entity to the data on the sub-dim entity

#if NODE_REGISTRY_MAP_TYPE_BOOST
#  ifdef STK_HAVE_TBB

    typedef tbb::scalable_allocator<std::pair<SubDimCell_SDCEntityType const, SubDimCellData> > RegistryAllocator;
    typedef boost::unordered_map<SubDimCell_SDCEntityType, SubDimCellData, my_fast_hash<SDCEntityType, 4>, my_fast_equal_to<SDCEntityType, 4>, RegistryAllocator > SubDimCellToDataMap;

#  else

    typedef boost::unordered_map<SubDimCell_SDCEntityType, SubDimCellData, my_fast_hash<SDCEntityType, 4>, my_fast_equal_to<SDCEntityType, 4> > SubDimCellToDataMap;
    typedef boost::unordered_map<stk::mesh::EntityId, EntityPtr > EntityRepo;

    typedef boost::tuple<const stk::mesh::Entity , stk::mesh::EntityRank, unsigned> ElementSideTuple;

    struct my_tuple_hash : public std::unary_function< ElementSideTuple, std::size_t>
    {
      inline std::size_t
      operator()(const ElementSideTuple& x) const
      {
        return std::size_t(x.get<0>().local_offset())+std::size_t(x.get<1>())+std::size_t(x.get<2>());
      }
    };

    typedef boost::unordered_map<ElementSideTuple, SubDimCellData, my_tuple_hash > ElementSideMap;

#  endif
#endif

    // Rank of sub-dim cells needing new nodes, which sub-dim entity, one non-owning element identifier, nodeId_elementOwnderId.first
    // FIXME - consider using bitfields to pack first two entries into a short - does this save anything on buffer size?
    enum CommDataTypeEnum {
      CDT_NEEDED_ENTITY_RANK,
      CDT_SUB_DIM_ENTITY_ORDINAL,
      CDT_NON_OWNING_ELEMENT_KEY
    };
    enum
      {
        NEW_NODE_IDS
      };
    // entity rank, ordinal of sub-dim entity, non-owning element key
    typedef boost::tuple<stk::mesh::EntityRank, unsigned, stk::mesh::EntityKey> CommDataType;

    enum NodeRegistryState {
      NRS_NONE,
      NRS_START_REGISTER_NODE,
      NRS_END_REGISTER_NODE,
      NRS_START_CHECK_FOR_REMOTE,
      NRS_END_CHECK_FOR_REMOTE,
      NRS_START_PARALLEL_OPS,
      NRS_START_GET_FROM_REMOTE,
      NRS_END_GET_FROM_REMOTE,
      NRS_END_PARALLEL_OPS
    };


    //========================================================================================================================
    //========================================================================================================================
    //========================================================================================================================
    class NodeRegistry
    {
      static const unsigned NR_MARK_NONE = 1u;
      static const unsigned NR_MARK = 2u;

    public:

      // FIXME use unordered_set
      typedef std::set<stk::mesh::Entity, stk::mesh::EntityLess> SetOfEntities;

      //========================================================================================================================
      // high-level interface

      NodeRegistry(percept::PerceptMesh& eMesh, bool useCustomGhosting = true) : m_eMesh(eMesh),
                                                  //m_comm_all(0),
                                                  m_comm_all( new stk::CommAll(eMesh.parallel()) ),
                                                  //m_comm_all(eMesh.get_bulk_data()->parallel()),
                                                  // why does this cause failures?
                                                  //m_cell_2_data_map(eMesh.get_number_elements()*8u),
                                                  m_pseudo_entities(stk::mesh::EntityLess(*eMesh.get_bulk_data())),
                                                  m_useCustomGhosting(useCustomGhosting),
                                                  m_gee_cnt(0), m_gen_cnt(0),
                                                  m_entity_repo(stk::percept::EntityRankEnd),
                                                  m_debug(false),
                                                  m_state(NRS_NONE)
      {
        m_useCustomGhosting = true;
      }

      ~NodeRegistry() {
        if (m_comm_all)
          delete m_comm_all;
      }

      void init_comm_all()
      {
        if (m_comm_all)
          delete m_comm_all;
        m_comm_all = new stk::CommAll(m_eMesh.parallel());
      }

      void init_entity_repo()
      {
        for (unsigned i = 0; i < stk::percept::EntityRankEnd; i++) m_entity_repo[i].clear();
      }

      void clear_dangling_nodes(SetOfEntities* nodes_to_be_deleted)
      {
        bool debug = false;
        if (debug) std::cout <<  "tmp srk NodeRegistry::clear_dangling_nodes start" << std::endl;
        double cpu_0 = stk::cpu_time();

        stk::mesh::BulkData &bulk_data = *m_eMesh.get_bulk_data();

        SubDimCellToDataMap::iterator iter;
        SubDimCellToDataMap& map = getMap();

        std::vector<const SubDimCell_SDCEntityType *> to_erase;
        int num_delete=0;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;
            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            stk::mesh::EntityRank      owning_element_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
            VERIFY_OP_ON(nodeIds_onSE.size(), ==, nodeIds_onSE.m_entity_id_vector.size(), "NodeRegistry::clear_dangling_nodes id vector/size mismatch");
            unsigned nnodes = nodeIds_onSE.size();
            NodeIdsOnSubDimEntityType node_to_keep(0);
            //std::vector<stk::mesh::Entity> node_to_keep;
            std::vector<stk::mesh::EntityId> node_id_to_keep(0);

            if (debug)
              {
                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
                if (!m_eMesh.is_valid(owning_element))
                  {
                    std::cout << "owning_element is invalid in clear_dangling_nodes  owning_elementId= " << owning_elementId << " owning_element_rank= " << owning_element_rank
                              << " subDimCellData.size=" << subDimEntity.size()
                              << std::endl;
                    throw std::logic_error("logic: clear_dangling_nodes hmmm #111.0");
                  }

              }

            if (debug)
            {
              std::cout << " nnodes= " << nnodes << " sd=" << m_eMesh.identifier(subDimEntity[0]) << " " << (nnodes>=2?m_eMesh.identifier(subDimEntity[1]):-1)
                        << " owning_elementId= " << owning_elementId
                        <<  std::endl;
            }
            for (unsigned inode=0; inode < nnodes; inode++)
              {
                stk::mesh::EntityId id = nodeIds_onSE.m_entity_id_vector[inode];
                if (!m_eMesh.is_valid(nodeIds_onSE[inode])) {
                  if (debug) std::cout << " invalid deleting " << id << " sd=" << m_eMesh.identifier(subDimEntity[0]) << " " << (nnodes>=2?m_eMesh.identifier(subDimEntity[1]):-1)
                                       << " owning_elementId= " << owning_elementId
                                       <<  std::endl;
                  continue;
                }
                stk::mesh::EntityId id_check = m_eMesh.identifier(nodeIds_onSE[inode]);
                VERIFY_OP_ON(id_check, ==, id, "NodeRegistry::clear_dangling_nodes id");

                if (nodes_to_be_deleted && nodes_to_be_deleted->find(nodeIds_onSE[inode]) != nodes_to_be_deleted->end())
                  {
                    if (debug) std::cout << " deleting " << id << " sd=" << m_eMesh.identifier(subDimEntity[0]) << " " << (nnodes>=2?m_eMesh.identifier(subDimEntity[1]):-1)
                                         << " owning_elementId= " << owning_elementId
                                         <<  std::endl;
                    ++num_delete;
                  }
                else if (!nodes_to_be_deleted && stk::mesh::Deleted == bulk_data.state(nodeIds_onSE[inode]) )
                  {
                    if (debug) std::cout << " 2 deleting " << id << " sd=" << m_eMesh.identifier(subDimEntity[0]) << " " <<  (nnodes>=2?m_eMesh.identifier(subDimEntity[1]):-1)
                                         << " owning_elementId= " << owning_elementId
                                         <<  std::endl;
                    ++num_delete;
                  }
                else
                  {
                    node_to_keep.push_back(nodeIds_onSE[inode]);
                    node_id_to_keep.push_back(id);
                  }
              }
            nodeIds_onSE = node_to_keep;
            nodeIds_onSE.m_entity_id_vector = node_id_to_keep;
            if (nodeIds_onSE.size() != nodeIds_onSE.m_entity_id_vector.size())
              {
                std::cout << "NodeRegistry::clear_dangling_nodes id vector/size mismatch 1 size= " << nodeIds_onSE.size() << " id.size= " << nodeIds_onSE.m_entity_id_vector.size() << std::endl;
              }
            VERIFY_OP_ON(nodeIds_onSE.size(), ==, nodeIds_onSE.m_entity_id_vector.size(), "NodeRegistry::clear_dangling_nodes id vector/size mismatch 1");

            if (nodeIds_onSE.size() == 0)
              {
                stk::mesh::EntityId id = nodeIds_onSE.m_entity_id_vector[0];
                if (debug) std::cout << " to_erase deleting " << id << " sd=" << m_eMesh.identifier(subDimEntity[0]) << " " << (nnodes>=2?m_eMesh.identifier(subDimEntity[1]):-1)
                                     << " owning_elementId= " << owning_elementId
                                     <<  std::endl;
                to_erase.push_back(&(iter->first));
              }

          }
        if (debug) std::cout << "tmp srk NodeRegistry::clear_dangling_nodes num_delete= " << num_delete <<  std::endl;
        if (to_erase.size())
          {
            s_compare_using_entity_impl = true;
            if (debug) std::cout << "tmp srk NodeRegistry::clear_dangling_nodes nodeIds_onSE.size() != node_to_keep.size()), to_erase= " << to_erase.size() <<  std::endl;
            for (unsigned i=0; i < to_erase.size(); i++)
              {
                if ( to_erase[i]->size() &&  map.find(*to_erase[i]) != map.end())
                  {
                    map.erase(*to_erase[i]);
                  }
              }
            s_compare_using_entity_impl = false;
          }

        // check
        if (1)
          {
            for (iter = map.begin(); iter != map.end(); ++iter)
              {
                SubDimCellData& nodeId_elementOwnderId = (*iter).second;
                NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                VERIFY_OP_ON(nodeIds_onSE.size(), ==, nodeIds_onSE.m_entity_id_vector.size(), "NodeRegistry::clear_dangling_nodes id vector/size mismatch after erase");
              }
          }
        double cpu_1 = stk::cpu_time();
        if (debug) std::cout <<  "tmp srk NodeRegistry::clear_dangling_nodes end, time= " << (cpu_1-cpu_0) << std::endl;
      }

      void clear_elements_to_be_deleted(SetOfEntities* elements_to_be_deleted)
      {
        bool debug = false;
        if (debug) std::cout <<  "tmp srk NodeRegistry::clear_elements_to_be_deleted start" << std::endl;

        SubDimCellToDataMap::iterator iter;
        SubDimCellToDataMap& map = getMap();

        std::vector<const SubDimCell_SDCEntityType *> to_erase;
        int num_delete=0;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;
            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            stk::mesh::EntityRank      owning_element_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
            VERIFY_OP_ON(nodeIds_onSE.size(), ==, nodeIds_onSE.m_entity_id_vector.size(), "NodeRegistry::clear_elements_to_be_deleted id vector/size mismatch");

            stk::mesh::Entity owning_element = stk::mesh::Entity();
            if (owning_elementId)
              {
                owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
                if (!m_eMesh.is_valid(owning_element))
                  {
                    std::cout << "owning_element is invalid in clear_elements_to_be_deleted  owning_elementId= " << owning_elementId << " owning_element_rank= " << owning_element_rank
                              << " subDimCellData.size=" << subDimEntity.size()
                              << std::endl;
                    throw std::logic_error("logic: clear_elements_to_be_deleted hmmm #211.0");
                  }
              }

            if (!owning_elementId || (elements_to_be_deleted && elements_to_be_deleted->find(owning_element) != elements_to_be_deleted->end()))
              {
                if (debug) std::cout << " deleting "
                                     << " owning_elementId= " << owning_elementId
                                     << " subDimEntity size= " << subDimEntity.size()
                                     <<  std::endl;
                ++num_delete;
                to_erase.push_back(&(iter->first));
              }
          }
        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;
            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            if (!owning_elementId) continue;
            stk::mesh::EntityRank      owning_element_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
            stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
            if (!m_eMesh.is_valid(owning_element))
              {
                std::cout << "owning_element is invalid in clear_elements_to_be_deleted  owning_elementId= " << owning_elementId << " owning_element_rank= " << owning_element_rank
                          << " subDimCellData.size=" << subDimEntity.size()
                          << std::endl;
                throw std::logic_error("logic: clear_elements_to_be_deleted hmmm #211.1");
              }
          }
        if (debug) std::cout << "tmp srk NodeRegistry::clear_elements_to_be_deleted num_delete= " << num_delete <<  std::endl;
        if (to_erase.size())
          {
            s_compare_using_entity_impl = true;
            for (unsigned i=0; i < to_erase.size(); i++)
              {
                if ( to_erase[i]->size() &&  map.find(*to_erase[i]) != map.end())
                  {
                    map.erase(*to_erase[i]);
                  }
              }
            s_compare_using_entity_impl = false;
          }

      }

      void initialize()
      {
        m_cell_2_data_map.clear();
        init_entity_repo();
      }

#define CHECK_DB 0
      void //NodeRegistry::
      beginRegistration()
      {
        if (CHECK_DB) checkDB("beginRegistration");

        m_nodes_to_ghost.resize(0);
        m_pseudo_entities.clear();
        m_state = NRS_START_REGISTER_NODE;
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::beginRegistration" << std::endl;
      }

      void //NodeRegistry::
      endRegistration()
      {
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endRegistration start" << std::endl;

        if (CHECK_DB) checkDB("endRegistration");

        removeUnmarkedSubDimEntities();

        if (CHECK_DB) checkDB("endRegistration - after removeUnmarkedSubDimEntities");

        m_eMesh.get_bulk_data()->modification_begin();
        this->createNewNodesInParallel();
        m_nodes_to_ghost.resize(0);

        if (CHECK_DB) checkDB("endRegistration - after createNewNodesInParallel");

#if STK_ADAPT_NODEREGISTRY_DO_REHASH
        m_cell_2_data_map.rehash(m_cell_2_data_map.size());
#endif
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endRegistration end" << std::endl;

        if (CHECK_DB) checkDB("endRegistration - after rehash");

        m_state = NRS_END_REGISTER_NODE;

      }

      void //NodeRegistry::
      beginLocalMeshMods()
      {
      }

      void //NodeRegistry::
      endLocalMeshMods()
      {
      }

      void //NodeRegistry::
      beginCheckForRemote()
      {
        m_state = NRS_START_CHECK_FOR_REMOTE;
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::beginCheckForRemote " << std::endl;
      }

      void //NodeRegistry::
      endCheckForRemote()
      {
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endCheckForRemote start " << std::endl;
        stk::ParallelMachine pm = m_eMesh.parallel();
        int failed = 0;
        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );

        this->allocateBuffers();

#if STK_ADAPT_NODEREGISTRY_DO_REHASH
        m_cell_2_data_map.rehash(m_cell_2_data_map.size());
#endif

        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endCheckForRemote end " << std::endl;

        m_state = NRS_END_CHECK_FOR_REMOTE;

      }

      void //NodeRegistry::
      beginGetFromRemote()
      {
        m_state = NRS_START_GET_FROM_REMOTE;
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::beginGetFromRemote  " << std::endl;

      }
      void //NodeRegistry::
      endGetFromRemote()
      {
        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endGetFromRemote start " << std::endl;
        stk::ParallelMachine pm = m_eMesh.parallel();
        int failed = 0;
        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );

        if (CHECK_DB) checkDB("endGetFromRemote before comm");

        this->communicate();

        failed = 0;
        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );

        if (m_useCustomGhosting)
        {
          stk::mesh::Ghosting & ghosting = m_eMesh.get_bulk_data()->create_ghosting( std::string("new_nodes") );
          vector<stk::mesh::EntityKey> receive;
          ghosting.receive_list( receive );
          m_eMesh.get_bulk_data()->change_ghosting( ghosting, m_nodes_to_ghost, receive);
        }

        failed = 0;
        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );

        m_eMesh.get_bulk_data()->modification_end();

        if (CHECK_DB) checkDB("endGetFromRemote after mod end");

        if (m_useCustomGhosting) setAllReceivedNodeData();

        if (CHECK_DB) checkDB("endGetFromRemote after setAllReceivedNodeData");

        if (m_debug)
          std::cout << "P[" << m_eMesh.get_rank() << "] tmp NodeRegistry::endGetFromRemote end " << std::endl;

        m_state = NRS_END_GET_FROM_REMOTE;
      }

      void setAllReceivedNodeData()
      {
        EXCEPTWATCH;
        SubDimCellToDataMap::iterator iter;
        stk::mesh::PartVector empty_parts;

        for (iter = m_cell_2_data_map.begin(); iter != m_cell_2_data_map.end(); ++iter)
          {
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();

            if (nodeIds_onSE.m_entity_id_vector.size() != nodeIds_onSE.size())
              {
                std::cout << " nodeIds_onSE.m_entity_id_vector.size() = " << nodeIds_onSE.m_entity_id_vector.size()
                          << " nodeIds_onSE.size() = " <<  nodeIds_onSE.size() << std::endl;

                throw std::logic_error("NodeRegistry:: setAllReceivedNodeData logic err #0");
              }

            for (unsigned ii = 0; ii < nodeIds_onSE.size(); ii++)
              {
                if (!nodeIds_onSE.m_entity_id_vector[ii])
                  {
                    nodeIds_onSE[ii] = stk::mesh::Entity();
                  }
                else
                  {
                    stk::mesh::Entity node = get_entity_node_I(*m_eMesh.get_bulk_data(), stk::mesh::MetaData::NODE_RANK, nodeIds_onSE.m_entity_id_vector[ii]);  // FIXME
                    if (0) std::cout << "tmp P[" << m_eMesh.get_rank() << "] NodeRegistry::setAllReceivedNodeData id= " << nodeIds_onSE.m_entity_id_vector[ii] << std::endl;
                    if (!m_eMesh.is_valid(node))
                      {
                        if (m_useCustomGhosting)
                          {
                            throw std::logic_error("NodeRegistry:: setAllReceivedNodeData logic err #3");
                          }
                        else
                          {
                            //std::cout << "tmp P[" << m_eMesh.get_rank() << "] NodeRegistry::setAllReceivedNodeData id= " << nodeIds_onSE.m_entity_id_vector[ii] << std::endl;
                            node = m_eMesh.get_bulk_data()->declare_entity(m_eMesh.node_rank(), nodeIds_onSE.m_entity_id_vector[ii], empty_parts);
                            if (!m_eMesh.is_valid(node)) throw std::logic_error("NodeRegistry:: setAllReceivedNodeData logic err #3.1");
                          }
                      }
                    if (!m_eMesh.is_valid(node)) throw std::logic_error("NodeRegistry:: setAllReceivedNodeData logic err #3.2");
                    nodeIds_onSE[ii] = node;
                  }
              }
          }
      }

      /// when a sub-dim entity is visited during node registration but is flagged as not being marked, and thus not requiring
      ///   any new nodes, we flag it with NR_MARK_NONE, then remove it here
      void removeUnmarkedSubDimEntities()
      {
        EXCEPTWATCH;
        SubDimCellToDataMap::iterator iter;
        bool debug = false;

        for (iter = m_cell_2_data_map.begin(); iter != m_cell_2_data_map.end(); ++iter)
          {
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
            if (nodeIds_onSE.size())
              {
                unsigned mark = nodeIds_onSE.m_mark;
                unsigned is_marked = mark & NR_MARK;
                unsigned is_not_marked = mark & NR_MARK_NONE;

                if (!is_marked && is_not_marked)
                  {
                    if (debug) std::cout << "tmp SRK FOUND mark = " << mark << " NR_MARK =" << NR_MARK << " NR_MARK_NONE= " << NR_MARK_NONE << std::endl;
                    // check if the node is a "hanging node" in which case it has relations
                    bool found = false;
                    for (unsigned in=0; in < nodeIds_onSE.size(); ++in)
                      {
                        if(!m_eMesh.is_valid(nodeIds_onSE[in])) continue;
                        if (debug) std::cout << "is valid = " << m_eMesh.is_valid(nodeIds_onSE[in]) << std::endl;
                        size_t num_rels = m_eMesh.get_bulk_data()->count_relations(nodeIds_onSE[in]);
                        if (num_rels)
                          {
                            if (debug) std::cout << "tmp SRK num_rels is non-zero in removeUnmarkedSubDimEntities, id= " << m_eMesh.identifier(nodeIds_onSE[in]) <<  std::endl;
                            found = true;
                            break;
                          }
                      }

                    if (!found)
                      {
                        nodeIds_onSE.resize(0);
                      }
                  }
              }
          }
      }

      bool is_empty( const stk::mesh::Entity element, stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd);

      /// Register the need for a new node on the sub-dimensional entity @param subDimEntity on element @param element.
      /// If the element is a ghost element, the entity is still registered: the locality/ownership of the new entity
      /// can be determined by the locality of the element (ghost or not).
      bool registerNeedNewNode(const stk::mesh::Entity element, NeededEntityType& needed_entity_rank, unsigned iSubDimOrd, bool needNodes)
      {
        bool ret_val = false;
        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, needed_entity_rank.first, iSubDimOrd);

        static SubDimCellData new_SubDimCellData;
        static SubDimCellData empty_SubDimCellData;

        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;
        bool is_not_empty_but_data_cleared = (!is_empty && nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().size() == 0);

        // if empty or if my id is the smallest, make this element the owner
        stk::mesh::EntityId db_id = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
        stk::mesh::EntityId db_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

        stk::mesh::EntityId element_id = m_eMesh.identifier(element);
        stk::mesh::EntityId element_rank = m_eMesh.entity_rank(element);
        bool should_put_in_id = (element_id < db_id);
        bool should_put_in_rank_gt = (element_rank > db_rank);
        bool should_put_in_rank_gte = (element_rank >= db_rank);
        bool should_put_in = should_put_in_rank_gt || (should_put_in_id && should_put_in_rank_gte);

        unsigned smark=0;
        if (!is_empty)
          {
            unsigned& mark = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().m_mark;
            if (needNodes)
              mark |= NR_MARK;
            else
              mark |= NR_MARK_NONE;
            smark = mark;
          }

        /// once it's in, the assertion should be:
        ///   owning_elementId < non_owning_elementId && owning_elementRank >= non_owning_elementRank
        ///
        if (is_empty || is_not_empty_but_data_cleared || should_put_in)
          {
            // new SubDimCellData SDC_DATA_OWNING_ELEMENT_KEY
            // CHECK

            unsigned numNewNodes = needed_entity_rank.second;

            NodeIdsOnSubDimEntityType nid_new(numNewNodes, stk::mesh::Entity(), smark);
            //NodeIdsOnSubDimEntityType nid_new(numNewNodes, stk::mesh::Entity(), 0u);
            if (needNodes)
              nid_new.m_mark |= NR_MARK;
            else
              nid_new.m_mark |= NR_MARK_NONE;

            smark = nid_new.m_mark;

            // create SubDimCellData for the map rhs
            // add one to iSubDimOrd for error checks later
            VERIFY_OP_ON(m_eMesh.identifier(element), !=, 0, "hmmm registerNeedNewNode #1");
            VERIFY_OP_ON(m_eMesh.is_valid(element), ==, true, "hmmm registerNeedNewNode #2");
            if (is_empty || is_not_empty_but_data_cleared)
              {
                SubDimCellData data(nid_new, stk::mesh::EntityKey(m_eMesh.entity_rank(element), m_eMesh.identifier(element)), iSubDimOrd+1 );
                putInMap(subDimEntity,  data);
              }
            else
              {
                NodeIdsOnSubDimEntityType& nid = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                SubDimCellData data(nid, stk::mesh::EntityKey(m_eMesh.entity_rank(element), m_eMesh.identifier(element)), iSubDimOrd+1 );
                putInMap(subDimEntity,  data);
              }
            ret_val = true;
          }

        if (DEBUG_NR_UNREF)
          {
            SubDimCellData* a_nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
            SubDimCellData& a_nodeId_elementOwnderId = (a_nodeId_elementOwnderId_ptr ? *a_nodeId_elementOwnderId_ptr : empty_SubDimCellData);
            bool a_is_empty = a_nodeId_elementOwnderId_ptr == 0;
            unsigned& gotMark = a_nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().m_mark;
            unsigned nidSize = a_nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().size();

            std::ostringstream sout;
            sout << "P[" << m_eMesh.get_rank() << "] registerNeedNewNode:: element= " << m_eMesh.identifier(element) << " nidSize= " << nidSize
                 << " nid= " << (nidSize ? m_eMesh.identifier(nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>()[0]) : -1);

            //m_eMesh.print(sout, element,false,true);
            sout << " smark= " << smark << " gotMark= " << gotMark << " needNodes= " << needNodes << " isG= " << m_eMesh.isGhostElement(element)
                 << " is_empty= " << is_empty
                 << " a_is_empty= " << a_is_empty
                      << " should_put_in= " << should_put_in
                      << " should_put_in_id= " << should_put_in_id
                      << " should_put_in_rank_gt= " << should_put_in_rank_gt
                      << " should_put_in_rank_gte= " << should_put_in_rank_gte
                      << " needed_entity_rank= "
                      << needed_entity_rank.first << " subDimEntity= ";

            for (unsigned k=0; k < subDimEntity.size(); k++)
              {
                sout << " " << m_eMesh.identifier(subDimEntity[k]) << " ";
              }
            std::cout << sout.str() << std::endl;
          }

        return ret_val;
      }


      void query(stk::mesh::EntityId elementId, unsigned rank, unsigned iSubDimOrd, std::string msg="")
      {
        stk::mesh::Entity element = m_eMesh.get_entity(m_eMesh.element_rank(), elementId);
        if (!m_eMesh.is_valid(element))
          {
            std::cout  << "P[" << m_eMesh.get_rank() << "] query:: " << msg << " element is not valid= " << elementId << std::endl;
            return;
          }
        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, rank, iSubDimOrd);

        static SubDimCellData new_SubDimCellData;
        static SubDimCellData empty_SubDimCellData;

        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;

        unsigned smark=0;
        if (!is_empty)
          {
            unsigned& mark = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().m_mark;
            smark = mark;
          }

        if (DEBUG_NR_DEEP) // && (m_eMesh.identifier(subDimEntity[0]) == 19 && m_eMesh.identifier(subDimEntity[1]) == 20))
          {
            unsigned& gotMark = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().m_mark;
            unsigned nidSize = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().size();

            std::ostringstream sout;
            sout << "P[" << m_eMesh.get_rank() << "] query:: " << msg << " element= " << m_eMesh.identifier(element) << " nidSize= " << nidSize
                 << " nid= " << (nidSize ? m_eMesh.identifier(nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>()[0]) : -1);
            sout << " smark= " << smark << " gotMark= " << gotMark << " isG= " << m_eMesh.isGhostElement(element)
                 << " is_empty= " << is_empty << " subDimEntity= ";

            for (unsigned k=0; k < subDimEntity.size(); k++)
              {
                sout << " " << m_eMesh.identifier(subDimEntity[k]) << " ";
              }
#if 0
            for (unsigned k=0; k < subDimEntity.size(); k++)
              {
                std::cout << " " << (stk::mesh::EntityId) subDimEntity[k] << " ";
              }
#endif
            std::cout << sout.str() << std::endl;
          }
      }

      /// Replace element ownership - FIXME for parallel
      /// When remeshing during unrefinement, replace ownership of sub-dim entities by non-deleted elements
      bool replaceElementOwnership(const stk::mesh::Entity element, NeededEntityType& needed_entity_rank, unsigned iSubDimOrd, bool needNodes)
      {
        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        noInline_getSubDimEntity(subDimEntity, element, needed_entity_rank.first, iSubDimOrd);

        static SubDimCellData new_SubDimCellData;
        static SubDimCellData empty_SubDimCellData;

        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;
        bool is_not_empty_but_data_cleared = (!is_empty && nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().size() == 0);
        (void)is_not_empty_but_data_cleared;

        // if empty or if my id is the smallest, make this element the owner
        stk::mesh::EntityId db_id = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
        stk::mesh::EntityId db_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

        bool should_put_in_id = (m_eMesh.identifier(element)  < db_id);
        bool should_put_in_rank_gt = (m_eMesh.entity_rank(element) > db_rank);
        bool should_put_in_rank_gte = (m_eMesh.entity_rank(element) >= db_rank);
        bool should_put_in = should_put_in_rank_gt || (should_put_in_id && should_put_in_rank_gte);
        if (db_id == 0u)
          should_put_in = true;

        if (!is_empty && should_put_in)
          {
            nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>() = stk::mesh::EntityKey(m_eMesh.entity_rank(element), m_eMesh.identifier(element));
            return true;
          }

        return false;
      }

      /// check the newly registered node from the registry, which does one of three things, depending on what mode we are in:
      ///   1. counts buffer in prep for sending (just does a pack)
      ///   2. packs the buffer (after buffers are alloc'd)
      ///   3. returns the new node after all communications are done
      bool checkForRemote(const stk::mesh::Entity element, NeededEntityType& needed_entity_rank, unsigned iSubDimOrd, bool needNodes_notUsed)
      {
        EXCEPTWATCH;
        static SubDimCellData empty_SubDimCellData;
        static CommDataType buffer_entry;

        bool isGhost = m_eMesh.isGhostElement(element);
        if (!isGhost) return true;

        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, needed_entity_rank.first, iSubDimOrd);

        stk::CommAll& comm_all = *m_comm_all;
        unsigned proc_rank = comm_all.parallel_rank();
        unsigned owner_proc_rank = m_eMesh.owner_rank(element);

        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;

        if (is_empty)
          {
            std::cout << "element= " << element
                      << " needed_entity_rank= " << needed_entity_rank.first<< " " << needed_entity_rank.second << std::endl;
            std::cout << "subDimEntity= " << subDimEntity << std::endl;
            std::cout << "nodeId_elementOwnderId= " << nodeId_elementOwnderId << std::endl;
            std::cout << "empty_SubDimCellData= " << empty_SubDimCellData << std::endl;
            throw std::logic_error("NodeRegistry::checkForRemote no data (is_empty=true) - logic error.");
            return false;
          }
        else
          {
            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            stk::mesh::EntityRank owning_elementRank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
            unsigned nidsz = nodeIds_onSE.size();

            // error check
            bool isNotOK = (m_eMesh.identifier(element) < owning_elementId) && (m_eMesh.entity_rank(element) > owning_elementRank) ;

            if ( isNotOK )
              {
                std::cout << "P[" << proc_rank << "] elem id = " << m_eMesh.identifier(element)
                          << " nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>() = "
                          << owning_elementId
                          << std::endl;
                throw std::logic_error("NodeRegistry::checkForRemote logic: owning element info is wrong");
              }

            stk::mesh::EntityRank erank = owning_elementRank;
            stk::mesh::Entity owning_element = get_entity_element(*m_eMesh.get_bulk_data(), erank, owning_elementId);

            if (!m_eMesh.is_valid(owning_element))
              {
                std::cout << "owning_elementId = " << owning_elementId << " erank = " << erank << std::endl;
                throw std::logic_error("NodeRegistry::checkForRemote logic: owning_element is null");
              }

            bool owning_element_is_ghost = m_eMesh.isGhostElement(owning_element);

            // if this element is a ghost, and the owning element of the node is not a ghost, send info
            //   to ghost element's owner proc
            if (!owning_element_is_ghost && isGhost)
              {
                buffer_entry = CommDataType(
                                            needed_entity_rank.first,
                                            iSubDimOrd,
                                            m_eMesh.key(element)
                                            );

                if (nodeIds_onSE.m_entity_id_vector.size() != nodeIds_onSE.size())
                  {
                    throw std::logic_error("NodeRegistry::checkForRemote logic err #0.1");
                  }

                for (unsigned iid = 0; iid < nidsz; iid++)
                  {
                    if (!m_eMesh.is_valid(nodeIds_onSE[iid]))
                      {
                        throw std::logic_error("logic: hmmm #5.0");
                      }

                    if (!nodeIds_onSE.m_entity_id_vector[iid])
                      {
                        throw std::logic_error("NodeRegistry::checkForRemote logic err #0.2");
                      }

                    //stk::mesh::Entity new_node = get_entity_node_Ia(*m_eMesh.get_bulk_data(), Node, nodeIds_onSE, iid);
                    stk::mesh::Entity new_node = nodeIds_onSE[iid];
                    if (!m_eMesh.is_valid(new_node))
                      {
                        throw std::logic_error("NodeRegistry::checkForRemote logic: new_node is null");
                      }

                    if (DEBUG_NR_DEEP)
                      {
                        SubDimCellData* a_nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
                        SubDimCellData& a_nodeId_elementOwnderId = (a_nodeId_elementOwnderId_ptr ? *a_nodeId_elementOwnderId_ptr : empty_SubDimCellData);
                        bool a_is_empty = a_nodeId_elementOwnderId_ptr == 0;
                        unsigned& gotMark = a_nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().m_mark;
                        unsigned nidSize = a_nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>().size();

                        std::ostringstream sout;
                        sout << "P[" << m_eMesh.get_rank() << "] checkForRemote:: element= " << m_eMesh.identifier(element) << " nidSize= " << nidSize;
                        //m_eMesh.print(sout, element,false,true);
                        sout << " isG= " << m_eMesh.isGhostElement(element)
                             << " gotMark= " << gotMark
                             << " a_is_empty= " << a_is_empty
                             << " needed_entity_rank= "
                             << needed_entity_rank.first << " subDimEntity= ";

                        for (unsigned k=0; k < subDimEntity.size(); k++)
                          {
                            sout << " " << m_eMesh.identifier(subDimEntity[k]) << " ";
                          }
#if 0
                        for (unsigned k=0; k < subDimEntity.size(); k++)
                          {
                            std::cout << " " << (stk::mesh::EntityId) subDimEntity[k] << " ";
                          }
#endif
                        sout << " : nodes_to_ghost: " << m_eMesh.identifier(new_node) << " element= " << m_eMesh.identifier(element)
                             << " element owner_proc_rank= " << owner_proc_rank
                             << " owning_elementId= " << owning_elementId ;

                        std::cout << sout.str() << std::endl;
                      }

                    if (m_eMesh.owner_rank(new_node) == m_eMesh.get_rank())
                      m_nodes_to_ghost.push_back( stk::mesh::EntityProc(new_node, owner_proc_rank) );
                  }

                  {
                    //std::cout << "P[" << proc_rank << "] : pack " << buffer_entry << " owner_proc_rank= " << owner_proc_rank << std::endl;
                    m_comm_all->send_buffer( owner_proc_rank ).pack< CommDataType > (buffer_entry);
                    NodeIdsOnSubDimEntityType& nids = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                    nids.pack(m_eMesh, m_comm_all->send_buffer( owner_proc_rank ));
                  }
              }
          }
        return true;
      }

      bool getFromRemote(const stk::mesh::Entity element, NeededEntityType& needed_entity_rank, unsigned iSubDimOrd, bool needNodes_notUsed)
      {
        return checkForRemote(element, needed_entity_rank, iSubDimOrd, needNodes_notUsed);
      }

      inline stk::mesh::Entity get_entity_using_find(stk::mesh::EntityRank& rank, const stk::mesh::EntityId& id) const
      {
        const EntityRepo::const_iterator i = m_entity_repo[rank].find( id );
        return i != m_entity_repo[rank].end() ? i->second : stk::mesh::Entity() ;
      }

      inline stk::mesh::Entity get_entity(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return bulk.get_entity(rank, id);
      }

      inline stk::mesh::Entity get_entity_I(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return bulk.get_entity(rank, id);
      }

      inline stk::mesh::Entity get_entity_Ia(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return bulk.get_entity(rank, id);
      }

      inline stk::mesh::Entity get_entity_Ib(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return bulk.get_entity(rank, id);
      }

      inline stk::mesh::Entity get_entity_element(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return get_entity(bulk, rank, id);
      }

      inline stk::mesh::Entity get_entity_node_I(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return get_entity_I(bulk, rank, id);
      }

      inline stk::mesh::Entity get_entity_node_Ia(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, NodeIdsOnSubDimEntityType& nodeIds_onSE, unsigned index)
      {
        stk::mesh::Entity entity = nodeIds_onSE[index];
        return entity;
      }

      inline stk::mesh::Entity get_entity_node_Ib(stk::mesh::BulkData& bulk, stk::mesh::EntityRank rank, stk::mesh::EntityId id)
      {
        return get_entity_Ib(bulk, rank, id);
      }

      NodeIdsOnSubDimEntityType* getNewNodesOnSubDimEntity(const stk::mesh::Entity element,  stk::mesh::EntityRank& needed_entity_rank, unsigned iSubDimOrd);

      /// makes coordinates of this new node be the centroid of its sub entity
      void makeCentroidCoords(const stk::mesh::Entity element,  stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd)
      {
        makeCentroidField(element, needed_entity_rank, iSubDimOrd, m_eMesh.get_coordinates_field());
      }

      void makeCentroidField(const stk::mesh::Entity element,  stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd, stk::mesh::FieldBase *field)
      {
        //EXCEPTWATCH;

        int spatialDim = m_eMesh.get_spatial_dim();
        stk::mesh::EntityRank field_rank = stk::mesh::MetaData::NODE_RANK;
        {
          unsigned nfr = field->restrictions().size();
          for (unsigned ifr = 0; ifr < nfr; ifr++)
            {
              const stk::mesh::FieldRestriction& fr = field->restrictions()[ifr];
              field_rank = fr . entity_rank();
              spatialDim = fr.dimension() ;
            }
        }

        if (field_rank != stk::mesh::MetaData::NODE_RANK)
          {
            return;
          }

        unsigned *null_u = 0;
        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, needed_entity_rank, iSubDimOrd);
        static SubDimCellData empty_SubDimCellData;

        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;

        if (s_allow_empty_sub_dims && is_empty)
          {
            return;
          }
        if (is_empty)
          {
            const CellTopologyData * const cell_topo_data = m_eMesh.get_cell_topology(*m_eMesh.get_bulk_data(), element);
            shards::CellTopology cell_topo(cell_topo_data);

            std::cout << "NodeRegistry::makeCentroidField: no node found, cell_topo = " << cell_topo.getName()
                      << "\n subDimEntity= " << subDimEntity
                      << "\n element= " << element
                      << "\n m_eMesh.entity_rank(element) = " << m_eMesh.entity_rank(element)
                      << "\n needed_entity_rank= " << needed_entity_rank
                      << "\n iSubDimOrd= " << iSubDimOrd << std::endl;
            throw std::runtime_error("makeCentroidField: no node found");
          }
        NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
        if (nodeIds_onSE.size() != 1)
          {
            throw std::logic_error("logic error: makeCentroidField not ready for multiple nodes, or there are 0 nodes on the marked quantity");
          }
        stk::mesh::Entity c_node = nodeIds_onSE[0];

        if (!m_eMesh.is_valid(c_node))
          {
            throw std::runtime_error("makeCentroidField: bad node found 0");
          }

        double c_p[] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

        bool doPrint = false;

        if (needed_entity_rank == stk::mesh::MetaData::ELEMENT_RANK)
          {
            const percept::MyPairIterRelation elem_nodes (m_eMesh, element, stk::mesh::MetaData::NODE_RANK);
            unsigned npts = elem_nodes.size();
            double dnpts = elem_nodes.size();
            for (unsigned ipts = 0; ipts < npts; ipts++)
              {
                stk::mesh::Entity node = elem_nodes[ipts].entity(); //m_eMesh.get_bulk_data()->get_entity(Node, nodeId);
                if (!m_eMesh.is_valid(node))
                  {
                    throw std::runtime_error("makeCentroidField: bad node found 1");
                  }
                double *  coord = m_eMesh.field_data(field, node, null_u);

                if (doPrint && coord)
                  {
                    const CellTopologyData * const cell_topo_data = m_eMesh.get_cell_topology(element);
                    shards::CellTopology cell_topo(cell_topo_data);

                    std::cout << "tmp NodeRegistry::makeCentroidField cell_topo = " << cell_topo.getName() << " ipts= " << ipts
                              << " coord= " << coord[0] << " " << coord[1] << " " << coord[2] << std::endl;
                  }

                if (coord)
                  {
                    for (int isp = 0; isp < spatialDim; isp++)
                      {
                        c_p[isp] += coord[isp]/dnpts;
                      }
                  }
              }

          }
        else
          {
            double dnpts = subDimEntity.size();
            for (SubDimCell_SDCEntityType::iterator ids = subDimEntity.begin(); ids != subDimEntity.end(); ids++)
              {
                SDCEntityType nodeId = *ids;

                stk::mesh::Entity node = nodeId;
                if (!m_eMesh.is_valid(node))
                  {
                    throw std::runtime_error("makeCentroidField: bad node found 2");
                  }
                double *  coord = m_eMesh.field_data(field, node, null_u);
                if (coord)
                  {
                    for (int isp = 0; isp < spatialDim; isp++)
                      {
                        c_p[isp] += coord[isp]/dnpts;
                      }
                  }
              }
          }
        double *  c_coord = m_eMesh.field_data(field, c_node, null_u);
        if (c_coord)
          {
            for (int isp = 0; isp < spatialDim; isp++)
              {
                c_coord[isp] = c_p[isp];
              }
            if (doPrint)
              {
                const CellTopologyData * const cell_topo_data = m_eMesh.get_cell_topology(element);
                shards::CellTopology cell_topo(cell_topo_data);

                std::cout << "tmp NodeRegistry::makeCentroidField cell_topo = " << cell_topo.getName()
                      << "\n subDimEntity= " << subDimEntity
                      << "\n element= " << element
                      << "\n m_eMesh.entity_rank(element) = " << m_eMesh.entity_rank(element)
                      << "\n needed_entity_rank= " << needed_entity_rank
                      << "\n iSubDimOrd= " << iSubDimOrd << std::endl;
              }
          }

      }

      double spacing_edge(std::vector<stk::mesh::Entity>& nodes,
                          unsigned iv0, unsigned iv1, unsigned nsz, unsigned nsp,  double lspc[8][3], double den_xyz[3], double *coord[8]);

      void normalize_spacing(stk::mesh::Entity element, std::vector<stk::mesh::Entity> &nodes,
                             unsigned nsz, unsigned nsp, double spc[8][3], double den_xyz[3], double *coord[8]);

      void makeCentroid(stk::mesh::FieldBase *field, unsigned *subDimSize_in=0);

      /// do interpolation for all fields
      void interpolateFields(const stk::mesh::Entity element,  stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd)
      {
        const stk::mesh::FieldVector & fields = m_eMesh.get_fem_meta_data()->get_fields();
        unsigned nfields = fields.size();
        for (unsigned ifld = 0; ifld < nfields; ifld++)
          {
            stk::mesh::FieldBase *field = fields[ifld];
            makeCentroidField(element, needed_entity_rank, iSubDimOrd, field);
          }
      }

      /// do interpolation for all fields
      void interpolateFields()
      {
        const stk::mesh::FieldVector & fields = m_eMesh.get_fem_meta_data()->get_fields();
        unsigned nfields = fields.size();
        for (unsigned ifld = 0; ifld < nfields; ifld++)
          {
            stk::mesh::FieldBase *field = fields[ifld];
            makeCentroid(field);
          }
      }

      /// check for adding new nodes to existing parts based on sub-entity part ownership
      void addToExistingParts(const stk::mesh::Entity element,  stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd)
      {
        const std::vector< stk::mesh::Part * > & parts = m_eMesh.get_fem_meta_data()->get_parts();

        unsigned nparts = parts.size();

        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, needed_entity_rank, iSubDimOrd);
        static  SubDimCellData empty_SubDimCellData;
        SubDimCellData* nodeId_elementOwnderId_ptr = getFromMapPtr(subDimEntity);
        SubDimCellData& nodeId_elementOwnderId = (nodeId_elementOwnderId_ptr ? *nodeId_elementOwnderId_ptr : empty_SubDimCellData);
        bool is_empty = nodeId_elementOwnderId_ptr == 0;

        if (s_allow_empty_sub_dims && is_empty)
          {
            return;
          }

        if (is_empty)
          {
            throw std::runtime_error("addToExistingParts: no node found");
          }
        NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
        unsigned nidsz = nodeIds_onSE.size();

        for (unsigned i_nid = 0; i_nid < nidsz; i_nid++)
          {
            stk::mesh::Entity c_node = nodeIds_onSE[i_nid];

            if (!m_eMesh.is_valid(c_node))
              {
                std::cout << "addToExistingParts: " <<  nodeIds_onSE[i_nid] << " i_nid= " << i_nid << " nidsz= " << nidsz
                          << " needed_entity_rank= " << needed_entity_rank << " iSubDimOrd= " << iSubDimOrd << std::endl;
                throw std::runtime_error("addToExistingParts: bad node found 0.1");
              }

            for (unsigned ipart=0; ipart < nparts; ipart++)
              {
                stk::mesh::Part& part = *parts[ipart];
                stk::mesh::Selector selector(part);

                if (stk::mesh::is_auto_declared_part(part))
                  continue;

                bool found = true;
                if (needed_entity_rank == stk::mesh::MetaData::ELEMENT_RANK)
                  {
                    const percept::MyPairIterRelation elem_nodes (m_eMesh, element, stk::mesh::MetaData::NODE_RANK);
                    unsigned npts = elem_nodes.size();
                    for (unsigned ipts = 0; ipts < npts; ipts++)
                      {
                        stk::mesh::Entity node = elem_nodes[ipts].entity();
                        if (!m_eMesh.is_valid(node))
                          {
                            throw std::runtime_error("addToExistingParts: bad node found 1.1");
                          }
                        if (!selector(m_eMesh.bucket(node)))
                          {
                            found = false;
                            break;
                          }

                      }
                  }
                else
                  {
                    for (SubDimCell_SDCEntityType::iterator ids = subDimEntity.begin(); ids != subDimEntity.end(); ++ids)
                      {
                        SDCEntityType nodeId = *ids;
                        stk::mesh::Entity node = nodeId;
                        if (!m_eMesh.is_valid(node))
                          {
                            throw std::runtime_error("addToExistingParts: bad node found 2.1");
                          }
                        if (!selector(m_eMesh.bucket(node)))
                          {
                            found = false;
                            break;
                          }
                      }
                  }
                if (found)
                  {
                    // add to part
                    std::vector<stk::mesh::Part*> add_parts(1, &part);
                    std::vector<stk::mesh::Part*> remove_parts;
                    const CellTopologyData *const topology = m_eMesh.get_cell_topology(part);
                    const unsigned part_rank = part.primary_entity_rank();

                    if (part_rank == stk::mesh::MetaData::NODE_RANK)
                      {
                        if (m_eMesh.bucket(c_node).owned())
                          m_eMesh.get_bulk_data()->change_entity_parts( c_node, add_parts, remove_parts );
                        if (0)
                          {
                            std::cout << "P[" << m_eMesh.get_rank() << "] adding node " << m_eMesh.identifier(c_node) << " to   Part[" << ipart << "]= " << part.name()
                                      << " topology = " << (topology ? shards::CellTopology(topology).getName() : "null")
                                      << std::endl;
                          }
                      }

                  }
              }
          }
      }

      void add_rbars(std::vector<std::vector<std::string> >& rbar_types );

      /// Check for adding new nodes to existing parts based on sub-entity part ownership.
      /// This version does it in bulk and thus avoids repeats on shared sub-dim entities.

      void addToExistingPartsNew()
      {
        static std::vector<stk::mesh::Part*> add_parts(1, static_cast<stk::mesh::Part*>(0));
        static std::vector<stk::mesh::Part*> remove_parts;

        const std::vector< stk::mesh::Part * > & parts = m_eMesh.get_fem_meta_data()->get_parts();

        unsigned nparts = parts.size();
        for (unsigned ipart=0; ipart < nparts; ipart++)
          {
            stk::mesh::Part& part = *parts[ipart];

            if (stk::mesh::is_auto_declared_part(part))
              continue;

            const CellTopologyData *const topology = m_eMesh.get_cell_topology(part);
            const unsigned part_rank = part.primary_entity_rank();

            if (part_rank == stk::mesh::MetaData::NODE_RANK)
              {
                stk::mesh::Selector selector(part);

                add_parts[0] = &part;

                SubDimCellToDataMap::iterator iter;

                for (iter = m_cell_2_data_map.begin(); iter != m_cell_2_data_map.end(); ++iter)
                  {
                    const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
                    SubDimCellData& nodeId_elementOwnderId = (*iter).second;

                    NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();

                    bool found = true;
                    stk::mesh::EntityRank needed_entity_rank = stk::mesh::MetaData::NODE_RANK;

                    // SPECIAL CASE
                    if( subDimEntity.size() == 1)
                      {
                        needed_entity_rank = stk::mesh::MetaData::ELEMENT_RANK;
                      }

                    if (needed_entity_rank == stk::mesh::MetaData::ELEMENT_RANK)
                      {
                        stk::mesh::Entity element_p = stk::mesh::Entity();
                        {
                          SDCEntityType elementId = subDimEntity[0];
                          element_p = elementId;
                          if (!m_eMesh.is_valid(element_p))
                            {
                                stk::mesh::EntityId        owning_elementId    = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
                                stk::mesh::EntityRank      owning_element_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
                                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
                                if (!m_eMesh.isGhostElement(owning_element))
                                  {
                                    std::cout << "subDimEntity size= " << subDimEntity.size() << std::endl;
                                    VERIFY_OP_ON(m_eMesh.is_valid(element_p), ==, true, "Bad elem found 2 in addToExistingPartsNew 2");
                                  }
                                continue;
                            }
                        }

                        stk::mesh::Entity element = element_p;

                        const percept::MyPairIterRelation elem_nodes (m_eMesh, element, stk::mesh::MetaData::NODE_RANK);
                        unsigned npts = elem_nodes.size();
                        for (unsigned ipts = 0; ipts < npts; ipts++)
                          {
                            stk::mesh::Entity node = elem_nodes[ipts].entity();
                            if (!m_eMesh.is_valid(node))
                              {
                                std::cout << "subDimEntity size= " << subDimEntity.size() << std::endl;
                              }
                            VERIFY_OP_ON(m_eMesh.is_valid(node), ==, true, "Bad node in addToExistingPartsNew 1");
                            if (!selector(m_eMesh.bucket(node)))
                              {
                                found = false;
                                break;
                              }
                          }
                      }
                    else
                      {
                        for (SubDimCell_SDCEntityType::const_iterator ids = subDimEntity.begin(); ids != subDimEntity.end(); ++ids)
                          {
                            SDCEntityType nodeId = *ids;
                            stk::mesh::Entity node = nodeId;
                            if (!m_eMesh.is_valid(node))
                              {
                                stk::mesh::EntityId        owning_elementId    = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
                                stk::mesh::EntityRank      owning_element_rank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
                                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
                                if (!m_eMesh.isGhostElement(owning_element))
                                  {
                                    std::cout << "subDimEntity size= " << subDimEntity.size() << std::endl;
                                    VERIFY_OP_ON(m_eMesh.is_valid(node), ==, true, "Bad node in addToExistingPartsNew 2");
                                  }
                                continue;
                              }
                            if (!selector(m_eMesh.bucket(node)))
                              {
                                found = false;
                                break;
                              }
                          }
                      }
                    if (found)
                      {
                        // add to part

                        unsigned nidsz = nodeIds_onSE.size();

                        for (unsigned i_nid = 0; i_nid < nidsz; i_nid++)
                          {
                            stk::mesh::Entity c_node = nodeIds_onSE[i_nid];

                            if (!m_eMesh.is_valid(c_node))
                              {
                                // note, this is ok - a null node here can come from a ghost element
                                continue;
                              }

                            // only try to add to part if I am the owner
                            if (m_eMesh.owner_rank(c_node) == m_eMesh.get_parallel_rank())
                              m_eMesh.get_bulk_data()->change_entity_parts( c_node, add_parts, remove_parts );

                            if (0)
                              {
                                std::cout << "P[" << m_eMesh.get_rank() << "] adding node " << m_eMesh.identifier(c_node) << " to   Part[" << ipart << "]= " << part.name()
                                          << " topology = " << (topology ? shards::CellTopology(topology).getName() : "null")
                                          << std::endl;
                              }
                          }

                      }
                  }
              }
          }
        //std::cout << "tmp addToExistingPartsNew...done " << std::endl;
      }

      SubDimCellData& getNewNodeAndOwningElement(SubDimCell_SDCEntityType& subDimEntity)
      {
        return m_cell_2_data_map[subDimEntity];
      }

      SubDimCellData * getFromMapPtr(const SubDimCell_SDCEntityType& subDimEntity) const
      {
        const SubDimCellToDataMap::const_iterator i = m_cell_2_data_map.find( subDimEntity );
        return i != m_cell_2_data_map.end() ? (const_cast<SubDimCellData *>(&(i->second))) : 0 ;
      }

      SubDimCellData& getFromMap(const SubDimCell_SDCEntityType& subDimEntity) const
      {
        const SubDimCellToDataMap::const_iterator i = m_cell_2_data_map.find( subDimEntity );
        return * (const_cast<SubDimCellData *>(&(i->second)));
      }

      void putInMap(SubDimCell_SDCEntityType& subDimEntity, SubDimCellData& data)
      {
        m_cell_2_data_map[subDimEntity] = data;
      }

      /// @param needNodes should be true in general; it's used by registerNeedNewNode to generate actual data or not on the subDimEntity
      ///   For local refinement, subDimEntity's needs are not always known uniquely by the pair {elementId, iSubDimOrd}; for example, in
      ///   an element-based marking scheme, the shared face between two elements may be viewed differently.  So, we need the ability to
      ///   override the default behavior of always creating new nodes on the subDimEntity, but still allow the entity to be created in
      ///   the NodeRegistry databse.

      typedef bool (NodeRegistry::*ElementFunctionPrototype)( const stk::mesh::Entity element, NeededEntityType& needed_entity_rank, unsigned iSubDimOrd, bool needNodes);

      /// this is a helper method that loops over all sub-dimensional entities whose rank matches on of those in @param needed_entity_ranks
      ///    and registers that sub-dimensional entity as needing a new node.
      /// @param isGhost should be true if this element is a ghost, in which case this will call the appropriate method to set up for
      //     communications

      void //NodeRegistry::
      doForAllSubEntities(ElementFunctionPrototype function, const stk::mesh::Entity element, vector<NeededEntityType>& needed_entity_ranks)
      {
        const CellTopologyData * const cell_topo_data = m_eMesh.get_cell_topology(element);

        shards::CellTopology cell_topo(cell_topo_data);

        for (unsigned ineed_ent=0; ineed_ent < needed_entity_ranks.size(); ineed_ent++)
          {
            unsigned numSubDimNeededEntities = 0;
            stk::mesh::EntityRank needed_entity_rank = needed_entity_ranks[ineed_ent].first;

            if (needed_entity_rank == m_eMesh.edge_rank())
              {
                numSubDimNeededEntities = cell_topo_data->edge_count;
              }
            else if (needed_entity_rank == m_eMesh.face_rank())
              {
                numSubDimNeededEntities = cell_topo_data->side_count;
              }
            else if (needed_entity_rank == stk::mesh::MetaData::ELEMENT_RANK)
              {
                numSubDimNeededEntities = 1;
              }

            for (unsigned iSubDimOrd = 0; iSubDimOrd < numSubDimNeededEntities; iSubDimOrd++)
              {
                /// note: at this level of granularity we can do single edge refinement, hanging nodes, etc.
                //SubDimCell_SDCEntityType subDimEntity;
                //getSubDimEntity(subDimEntity, element, needed_entity_rank, iSubDimOrd);
                (this ->* function)(element, needed_entity_ranks[ineed_ent], iSubDimOrd, true);

              } // iSubDimOrd
          } // ineed_ent
      }

      void //NodeRegistry::
      noInline_getSubDimEntity(SubDimCell_SDCEntityType& subDimEntity, const stk::mesh::Entity element, stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd);

      /// fill
      ///    @param subDimEntity with the stk::mesh::EntityId's of
      ///    the ordinal @param iSubDimOrd sub-dimensional entity of
      ///    @param element of rank
      ///    @param needed_entity_rank
      ///
      void //NodeRegistry::
      getSubDimEntity(SubDimCell_SDCEntityType& subDimEntity, const stk::mesh::Entity element, stk::mesh::EntityRank needed_entity_rank, unsigned iSubDimOrd)
      {
        subDimEntity.clear();
        // in the case of elements, we don't share any nodes so we just make a map of element id to node
        if (needed_entity_rank == stk::mesh::MetaData::ELEMENT_RANK)
          {
            subDimEntity.insert( element );
            return;
          }

        const CellTopologyData * const cell_topo_data = m_eMesh.get_cell_topology(element);

        stk::mesh::Entity const * const elem_nodes = m_eMesh.get_bulk_data()->begin_nodes(element);

        const unsigned *  inodes = 0;
        unsigned nSubDimNodes = 0;
        static const unsigned edge_nodes_2[2] = {0,1};
        static const unsigned face_nodes_3[3] = {0,1,2};
        static const unsigned face_nodes_4[4] = {0,1,2,3};

        // special case for faces in 3D
        if (needed_entity_rank == m_eMesh.face_rank() && needed_entity_rank == m_eMesh.entity_rank(element))
          {
            nSubDimNodes = cell_topo_data->vertex_count;

            // note, some cells have sides with both 3 and 4 nodes (pyramid, prism)
            if (nSubDimNodes ==3 )
              inodes = face_nodes_3;
            else
              inodes = face_nodes_4;

          }
        // special case for edges in 2D
        else if (needed_entity_rank == m_eMesh.edge_rank() && needed_entity_rank == m_eMesh.entity_rank(element))
          {
            nSubDimNodes = cell_topo_data->vertex_count;

            if (nSubDimNodes == 2 )
              {
                inodes = edge_nodes_2;
              }
            else
              {
                throw std::runtime_error("NodeRegistry bad for edges");
              }
          }
        else if (needed_entity_rank == m_eMesh.edge_rank())
          {
            inodes = cell_topo_data->edge[iSubDimOrd].node;
            nSubDimNodes = 2;
          }
        else if (needed_entity_rank == m_eMesh.face_rank())
          {
            nSubDimNodes = cell_topo_data->side[iSubDimOrd].topology->vertex_count;
            // note, some cells have sides with both 3 and 4 nodes (pyramid, prism)
            inodes = cell_topo_data->side[iSubDimOrd].node;
          }

        //subDimEntity.reserve(nSubDimNodes);
        for (unsigned jnode = 0; jnode < nSubDimNodes; jnode++)
          {
            subDimEntity.insert( elem_nodes[inodes[jnode]] );
          }
      }

      unsigned total_size()
      {
        unsigned sz=0;

        for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
          {
            SubDimCellData& data = (*cell_iter).second;
            NodeIdsOnSubDimEntityType& nodeIds_onSE = data.get<SDC_DATA_GLOBAL_NODE_IDS>();

            sz += nodeIds_onSE.size();
          }
        return sz;
      }

      unsigned local_size()
      {
        unsigned sz=0;
        for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
          {
            SubDimCellData& data = (*cell_iter).second;

            stk::mesh::EntityId owning_elementId = data.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();

            NodeIdsOnSubDimEntityType& nodeIds_onSE = data.get<SDC_DATA_GLOBAL_NODE_IDS>();
            if (nodeIds_onSE.size())
              {
                stk::mesh::EntityRank erank = data.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
                stk::mesh::Entity owning_element = get_entity_element(*m_eMesh.get_bulk_data(), erank, owning_elementId);

                if (!m_eMesh.is_valid(owning_element))
                  {
                    std::cout << "tmp owning_element = null, owning_elementId= " << owning_elementId
                              << std::endl;
                    throw std::logic_error("logic: hmmm #5.2");
                  }
                if (!m_eMesh.isGhostElement(owning_element))
                  {
                    sz += nodeIds_onSE.size();
                  }
              }
          }
        return sz;
      }

      //========================================================================================================================
      // low-level interface
      // FIXME

      void checkDB(std::string msg="")
      {
        //if ((m_eMesh.identifier(element) == 253 || m_eMesh.identifier(element) == 265) && needed_entity_rank.first==1
        //    && m_eMesh.identifier(subDimEntity[0]) == 65 && m_eMesh.identifier(subDimEntity[1]) == 141)
        if (DEBUG_NR_DEEP)
          {
            //query(265, 1, 2, msg);
            //query(253, 1, 0, msg);
          }

        if (0)
          {
            unsigned sz=0;
            for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
              {
                SubDimCellData& data = (*cell_iter).second;
                stk::mesh::EntityId owning_elementId = data.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
                NodeIdsOnSubDimEntityType& nodeIds_onSE = data.get<SDC_DATA_GLOBAL_NODE_IDS>();

                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(stk::mesh::MetaData::ELEMENT_RANK, owning_elementId);
                if (!m_eMesh.is_valid(owning_element))
                  throw std::logic_error("logic: hmmm #5.3");
                bool isGhost = m_eMesh.isGhostElement(owning_element);
                if (!m_eMesh.isGhostElement(owning_element))
                  {
                    ++sz;
                  }
                //if (!isGhost)
                std::ostringstream str;
                const SubDimCell_SDCEntityType& subDimEntity = (*cell_iter).first;
                str << "subDimEntity.size() = " << subDimEntity.size();
                for (unsigned i=0; i < subDimEntity.size(); i++)
                  {
                    str << " s[" << i << "] = " << m_eMesh.identifier(subDimEntity[i]);
                  }

                std::cout << "P[" << m_eMesh.get_rank() << "] owning_elementId = "  << owning_elementId << " isGhostElement = " << isGhost
                          << " nodeId = " << nodeIds_onSE << " subDimEntity= " << str.str() << std::endl;
              }
          }
        if (0)
          {
            std::cout << "P[" << m_eMesh.get_rank() << "] NodeRegistry::checkDB start msg= " << msg << " m_cell_2_data_map.size= " << m_cell_2_data_map.size() << std::endl;
            for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
              {
                const SubDimCell_SDCEntityType& subDimEntity = (*cell_iter).first;
                SubDimCellData&            subDimCellData      = (*cell_iter).second;
                stk::mesh::EntityId        owning_elementId    = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
                stk::mesh::EntityRank      owning_element_rank = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);

                NodeIdsOnSubDimEntityType& nodeIds_onSE        = subDimCellData.get<SDC_DATA_GLOBAL_NODE_IDS>();

                int count=0;
                for (SubDimCell_SDCEntityType::const_iterator ids = subDimEntity.begin(); ids != subDimEntity.end(); ++ids, ++count)
                  {
                    SDCEntityType nodeId = *ids;
                    stk::mesh::Entity node = nodeId;
                    if (!m_eMesh.is_valid(node))
                      {
                        std::cout << "subDimEntity size= " << subDimEntity.size() << " count= " << count << std::endl;
                      }
                    VERIFY_OP_ON(m_eMesh.is_valid(node), ==, true, "Bad node in checkDB 1");
                  }

                for (unsigned i=0; i < nodeIds_onSE.size(); i++)
                  {
                    stk::mesh::Entity node = nodeIds_onSE[i];
                    stk::mesh::EntityId nodeId = nodeIds_onSE.m_entity_id_vector[i];
                    if (!m_eMesh.is_valid(node)) {
                      std::cout << "i= " << i << " size= " << nodeIds_onSE.size()
                                << " node= " << node << " nodeId= " << nodeId
                                << " subDimEntity.size= " << subDimEntity.size()
                                << " owning_elementId = " << owning_elementId
                                << " owning_element_rank = " << owning_element_rank
                                << " owning_element = " << owning_element
                                << " owning_element.isGhostElement= " << m_eMesh.isGhostElement(owning_element)
                                << std::endl;
                    }
                    VERIFY_OP_ON(node, !=, stk::mesh::Entity(), "checkDB #11.1");
                    VERIFY_OP_ON(nodeId, !=, 0, "checkDB #11.1.1");
                    VERIFY_OP_ON(m_eMesh.identifier(node), ==, nodeId, "checkDB #11.2");
                    stk::mesh::Entity node_0 = m_eMesh.get_bulk_data()->get_entity(0, nodeId);

                    VERIFY_OP_ON(node, ==, node_0, "checkDB #11.3");
                    VERIFY_OP_ON(m_eMesh.identifier(node_0), ==, nodeId, "checkDB #11.4");
                  }
              }
            std::cout << "P[" << m_eMesh.get_rank() << "] NodeRegistry::checkDB end msg= " << msg << std::endl;
          }

        if (1)
          {
            std::cout << "P[" << m_eMesh.get_rank() << "] NodeRegistry::checkDB start 1 msg= " << msg << std::endl;
            for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
              {
                const SubDimCell_SDCEntityType& subDimEntity = (*cell_iter).first;
                SubDimCellData&            subDimCellData      = (*cell_iter).second;
                stk::mesh::EntityId        owning_elementId    = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
                stk::mesh::EntityRank      owning_element_rank = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
                NodeIdsOnSubDimEntityType& nodeIds_onSE        = subDimCellData.get<SDC_DATA_GLOBAL_NODE_IDS>();

                VERIFY_OP_ON(owning_elementId , !=, 0, "owning_elementId=0");
                if (owning_elementId == 0) continue;
                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_element_rank, owning_elementId);
                if (!m_eMesh.is_valid(owning_element))
                  {
                    std::cout << "owning_element is invalid, msg= " << msg << " owning_elementId= " << owning_elementId << " owning_element_rank= " << owning_element_rank
                              << " subDimCellData.size=" << subDimEntity.size()
                              << std::endl;
                    throw std::logic_error("logic: checkDB hmmm #11.0");
                  }
                //bool isGhost = m_eMesh.isGhostElement(*owning_element);
                if (0 && !m_eMesh.isGhostElement(owning_element))
                  {
                    for (unsigned i=0; i < nodeIds_onSE.size(); i++)
                      {
                        stk::mesh::Entity node = nodeIds_onSE[i];
                        stk::mesh::EntityId nodeId = nodeIds_onSE.m_entity_id_vector[i];
                        VERIFY_OP_ON(node, !=, stk::mesh::Entity(), "checkDB #11.1");
                        VERIFY_OP_ON(nodeId, !=, 0, "checkDB #11.1.1");
                        VERIFY_OP_ON(m_eMesh.identifier(node), ==, nodeId, "checkDB #11.2");
                        stk::mesh::Entity node_0 = m_eMesh.get_bulk_data()->get_entity(0, nodeId);

                        VERIFY_OP_ON(node, ==, node_0, "checkDB #11.3");
                        VERIFY_OP_ON(m_eMesh.identifier(node_0), ==, nodeId, "checkDB #11.4");
                        const int owner_rank = m_eMesh.owner_rank(node);
                        VERIFY_OP_ON(owner_rank, ==, m_eMesh.owner_rank(owning_element), "checkDB #11.6");
                      }
                  }
              }
            std::cout << "P[" << m_eMesh.get_rank() << "] NodeRegistry::checkDB end 1 msg= " << msg << std::endl;

          }

      }

      /// allocate the send/recv buffers for all-to-all communication
      bool allocateBuffers()
      {
        stk::CommAll& comm_all = *m_comm_all;
        unsigned proc_size = comm_all.parallel_size();
        unsigned proc_rank = comm_all.parallel_rank();

        // FIXME - add some error checking

        bool local = true; // FIXME
        unsigned num_msg_bounds = proc_size < 4 ? proc_size : proc_size/4 ;
        bool global = comm_all.allocate_buffers(num_msg_bounds , false, local );
        if ( not global )
          {
            std::cout << "P[" << proc_rank << "] : not global" << std::endl;
            return false;
          }
        return true;
      }

      void communicate()
      {
        stk::CommAll& comm_all = *m_comm_all;
        //unsigned proc_size = comm_all.parallel_size();
        //unsigned proc_rank = comm_all.parallel_rank();

#if 0
        for (unsigned i_proc_rank = 0; i_proc_rank < proc_size; i_proc_rank++)
          {
            std::cout << "P[" << proc_rank << "] : i_proc_rank = " << i_proc_rank << " send buf size =  "
                      <<   m_comm_all->send_buffer( i_proc_rank ).size() << " num in buf= "
                      <<   m_comm_all->send_buffer( i_proc_rank ).size() / sizeof(CommDataType) <<  std::endl;
          }
#endif
        comm_all.communicate();

        stk::ParallelMachine pm = m_eMesh.parallel();
        int failed = 0;
        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );

        unpack();

      }

      void
      unpack()
      {
        stk::CommAll& comm_all = *m_comm_all;

        int failed = 0;
        std::string msg;

        stk::ParallelMachine pm = m_eMesh.parallel();
        unsigned proc_size = m_eMesh.get_parallel_size();
        unsigned proc_rank = comm_all.parallel_rank();

        vector<stk::mesh::EntityProc> nodes_to_ghost;

        CommDataType buffer_entry;
        NodeIdsOnSubDimEntityType nodeIds_onSE;
        try
          {
            for(unsigned from_proc = 0; from_proc < proc_size; ++from_proc )
              {
                stk::CommBuffer & recv_buffer = comm_all.recv_buffer( from_proc );

                //unsigned num_in_buffer = recv_buffer.size() / sizeof(CommDataType);
                //std::cout << "for proc= " << from_proc << " recv_buffer.size()= " << recv_buffer.size() << " num_in_buffer = " << num_in_buffer << std::endl;

                while ( recv_buffer.remaining() )
                  {
                    //
                    // this->unpack( this->container(), p, recv_buffer );
                    //
                    // Rank of sub-dim cells needing new nodes, which sub-dim entity, one non-owning element identifier, nodeId_elementOwnderId.first
                    // typedef boost::tuple::tuple<stk::mesh::EntityRank, unsigned, stk::mesh::EntityId, stk::mesh::EntityId> CommDataType;


                    recv_buffer.unpack< CommDataType >( buffer_entry );
                    nodeIds_onSE.unpack(m_eMesh, recv_buffer);

                    //if(1) std::cout << "P[" << proc_rank << "] unpack for buffer from proc= " << from_proc << " " << buffer_entry << std::endl;
                    createNodeAndConnect(buffer_entry, nodeIds_onSE, from_proc, nodes_to_ghost);
                  }
              }
          }
        catch ( std::exception &x )
          {
            failed = 1;
            msg = std::string("unpack error: ")+x.what();
          }

        stk::all_reduce( pm, stk::ReduceSum<1>( &failed ) );
        if ( failed )
          {
            throw std::runtime_error( msg+" from unpack error, rank = "+toString(proc_rank) );
          }

        if (nodes_to_ghost.size())
          {
            stk::mesh::Ghosting & ghosting = m_eMesh.get_bulk_data()->create_ghosting( std::string("new_nodes") );

            vector<stk::mesh::EntityKey> receive;
            ghosting.receive_list( receive );
            //if (receive.size()) std::cout << "NodeRegistry::endGetFromRemote receive.size() = " << receive.size() << std::endl;
            m_eMesh.get_bulk_data()->change_ghosting( ghosting, nodes_to_ghost, receive);
          }

      }// unpack


      /// after registering all needed nodes, this method is used to request new nodes on this processor
      void createNewNodesInParallel()
      {
        static int num_times_called = 0;
        ++num_times_called;

        stk::mesh::Part* new_nodes_part = m_eMesh.get_non_const_part("refine_new_nodes_part");
        if (new_nodes_part)
          {
            // first remove any existing nodes
            //unsigned proc_rank = m_eMesh.get_rank();
            //std::cout << "P[" << proc_rank << "] remove existing nodes... " <<  std::endl;
            std::vector<stk::mesh::Part*> remove_parts(1, new_nodes_part);
            std::vector<stk::mesh::Part*> add_parts;
            std::vector<stk::mesh::Entity> node_vec;

            stk::mesh::Selector removePartSelector(*new_nodes_part & m_eMesh.get_fem_meta_data()->locally_owned_part() );
            const std::vector<stk::mesh::Bucket*> & buckets = m_eMesh.get_bulk_data()->buckets( m_eMesh.node_rank() );
            for ( std::vector<stk::mesh::Bucket*>::const_iterator k = buckets.begin() ; k != buckets.end() ; ++k )
              {
                stk::mesh::Bucket & bucket = **k ;
                if (removePartSelector(bucket))
                  {
                    const unsigned num_entity_in_bucket = bucket.size();
                    for (unsigned ientity = 0; ientity < num_entity_in_bucket; ientity++)
                      {
                        stk::mesh::Entity node = bucket[ientity];
                        node_vec.push_back(node);
                      }
                  }
              }
            for (unsigned ii=0; ii < node_vec.size(); ii++)
              {
                m_eMesh.get_bulk_data()->change_entity_parts( node_vec[ii], add_parts, remove_parts );
              }

            //std::cout << "P[" << proc_rank << "] remove existing nodes...done " <<  std::endl;
          }

        unsigned num_nodes_needed = local_size();

        // FIXME
        // assert( bulk data is in modifiable mode)
        // create new entities on this proc
        vector<stk::mesh::Entity> new_nodes;

        if (m_useCustomGhosting)
          {
            m_eMesh.createEntities( stk::mesh::MetaData::NODE_RANK, num_nodes_needed, new_nodes);

            if (1)
              {
                unsigned proc_rank = m_eMesh.get_rank();
                for (unsigned i=0; i < num_nodes_needed; i++)
                  {
                    if (0) std::cout << "P[" << proc_rank << "] i= " << i << " new node id = " << m_eMesh.identifier(new_nodes[i]) <<   std::endl;
                    VERIFY_OP_ON(m_eMesh.is_valid(new_nodes[i]), ==, true, "createNewNodesInParallel #23");
                  }
              }

          }

        std::vector<stk::mesh::EntityId> ids(num_nodes_needed);

        if (!m_useCustomGhosting)
          {
#define NR_GEN_OWN_IDS 0
#if NR_GEN_OWN_IDS
            new_nodes.resize(num_nodes_needed);
#else
            m_eMesh.createEntities( stk::mesh::MetaData::NODE_RANK, num_nodes_needed, new_nodes);
#endif

            // bogus, but just for testing - we delete the just-created entities and re-declare them without ownership info
            //   so that modification_end takes care of assigning ownership
            stk::mesh::PartVector empty_parts;
            for (unsigned i=0; i < num_nodes_needed; i++)
              {
#if NR_GEN_OWN_IDS
                ids[i] = (num_times_called*100000) + i + (num_times_called*100000)*1000*m_eMesh.get_parallel_rank();
#else
                ids[i] = m_eMesh.identifier(new_nodes[i]);
                bool did_destroy = m_eMesh.get_bulk_data()->destroy_entity(new_nodes[i]);
                VERIFY_OP_ON(did_destroy, ==, true, "createNewNodesInParallel couldn't destroy");
#endif
                {
                  unsigned proc_rank = m_eMesh.get_rank();
                  std::cout << "P[" << proc_rank << "] new node id =  " << ids[i] <<   std::endl;
                }

                new_nodes[i] = m_eMesh.get_bulk_data()->declare_entity(m_eMesh.node_rank(), ids[i], empty_parts);
              }
          }

        if (new_nodes_part)
          {
            //unsigned proc_rank = m_eMesh.get_rank();
            //std::cout << "P[" << proc_rank << "] add new nodes... " <<  std::endl;
            stk::mesh::Selector selector(m_eMesh.get_fem_meta_data()->locally_owned_part() );
            std::vector<stk::mesh::Part*> add_parts(1, new_nodes_part);
            std::vector<stk::mesh::Part*> remove_parts;
            for (unsigned ind = 0; ind < new_nodes.size(); ind++)
              {

                m_eMesh.get_bulk_data()->change_entity_parts( new_nodes[ind], add_parts, remove_parts );
              }
            //std::cout << "P[" << proc_rank << "] add new nodes...done " <<  std::endl;
          }

        // set map values to new node id's
        unsigned inode=0;

        for (SubDimCellToDataMap::iterator cell_iter = m_cell_2_data_map.begin(); cell_iter != m_cell_2_data_map.end(); ++cell_iter)
          {
            SubDimCellData& data = (*cell_iter).second;
            NodeIdsOnSubDimEntityType& nodeIds_onSE = data.get<SDC_DATA_GLOBAL_NODE_IDS>();
            if (!nodeIds_onSE.size())
              continue;

            stk::mesh::EntityId owning_elementId = data.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();

            if (!owning_elementId)
              {
                throw std::logic_error("logic: hmmm #5.4.0");
              }

            //!
            stk::mesh::EntityRank erank = stk::mesh::MetaData::ELEMENT_RANK;
            erank = data.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();
            stk::mesh::Entity owning_element = get_entity_element(*m_eMesh.get_bulk_data(), erank, owning_elementId);
            //!

            if (!m_eMesh.is_valid(owning_element))
              {
                throw std::logic_error("logic: hmmm #5.4");
              }
            if (!m_eMesh.isGhostElement(owning_element))
              {
                if (nodeIds_onSE.m_entity_id_vector.size() != nodeIds_onSE.size())
                  {
                    throw std::logic_error("NodeRegistry:: createNewNodesInParallel logic err #0.0");
                  }

                for (unsigned ii = 0; ii < nodeIds_onSE.size(); ii++)
                  {
                    //nodeIds_onSE[ii] = new_nodes[inode]->identifier();

                    VERIFY_OP(inode, < , num_nodes_needed, "UniformRefiner::doBreak() too many nodes");
                    if ( DEBUG_NR_UNREF)
                      {
                        std::cout << "tmp createNewNodesInParallel: old node id= " << (m_eMesh.is_valid(nodeIds_onSE[ii]) ? toString(m_eMesh.identifier(nodeIds_onSE[ii])) : std::string("null")) << std::endl;
                        std::cout << "tmp createNewNodesInParallel: new node=";
                        m_eMesh.print_entity(std::cout, new_nodes[inode]);
                      }

                    // if already exists from a previous iteration/call to doBreak, don't reset it and just use the old node
                    if (m_eMesh.is_valid(nodeIds_onSE[ii]))
                      {
                        if (DEBUG_NR_UNREF)
                          {
                            std::cout << "tmp createNewNodesInParallel: old node id is no-null, re-using it= " << (m_eMesh.is_valid(nodeIds_onSE[ii]) ? toString(m_eMesh.identifier(nodeIds_onSE[ii])) : std::string("null")) << std::endl;
                            std::cout << "tmp createNewNodesInParallel: new node=";
                            m_eMesh.print_entity(std::cout, new_nodes[inode]);
                          }
                      }
                    else
                      {
                        nodeIds_onSE[ii] = new_nodes[inode];
                        nodeIds_onSE.m_entity_id_vector[ii] = m_eMesh.identifier(new_nodes[inode]);
                      }

                    //nodeIds_onSE.m_entity_vector[ii] = new_nodes[inode];
                    inode++;
                  }
                //data.get<SDC_DATA_GLOBAL_NODE_IDS>()[0] = new_nodes[inode]->identifier();
              }
          }
      }


      /// unpacks the incoming information in @param buffer_entry and adds that information to my local node registry
      /// (i.e. the map of sub-dimensional entity to global node id is updated)
      void
      createNodeAndConnect(CommDataType& buffer_entry, NodeIdsOnSubDimEntityType& nodeIds_onSE, unsigned from_proc, vector<stk::mesh::EntityProc>& nodes_to_ghost)
      {
        //stk::mesh::EntityId&                  non_owning_elementId = buffer_entry.get<CDT_NON_OWNING_ELEMENT_KEY>();

        stk::mesh::EntityRank& needed_entity_rank                    = buffer_entry.get<CDT_NEEDED_ENTITY_RANK>();
        unsigned    iSubDimOrd                                       = buffer_entry.get<CDT_SUB_DIM_ENTITY_ORDINAL>();
        stk::mesh::EntityKey&  non_owning_elementKey                 = buffer_entry.get<CDT_NON_OWNING_ELEMENT_KEY>();
        stk::mesh::EntityId    non_owning_elementId                  = non_owning_elementKey.id();
        stk::mesh::EntityRank  non_owning_elementRank                = non_owning_elementKey.rank();

        // create a new relation here?  no, we are going to delete this element, so we just register that the new node is attached to
        //stk::mesh::Entity element = m_eMesh.get_bulk_data()->get_entity(stk::mesh::MetaData::ELEMENT_RANK, non_owning_elementId);

        //!
        stk::mesh::EntityRank erank = stk::mesh::MetaData::ELEMENT_RANK;
        erank = non_owning_elementRank;
        stk::mesh::Entity element = get_entity_element(*m_eMesh.get_bulk_data(), erank, non_owning_elementId);
        //!

        for (unsigned iid = 0; iid < nodeIds_onSE.size(); iid++)
          {
            stk::mesh::Entity node = nodeIds_onSE[iid];

            // has to be null, right?
            if (m_eMesh.is_valid(node))
              {
                throw std::logic_error("logic: node should be null in createNodeAndConnect");
              }
          }
        if (!m_eMesh.is_valid(element))
          {
            throw std::logic_error("logic: element shouldn't be null in createNodeAndConnect");
          }

        SubDimCell_SDCEntityType subDimEntity(m_eMesh);
        getSubDimEntity(subDimEntity, element, needed_entity_rank, iSubDimOrd);
        SubDimCellData& subDimCellData = getNewNodeAndOwningElement(subDimEntity);
        // assert it is empty?

        subDimCellData.get<SDC_DATA_GLOBAL_NODE_IDS>() = nodeIds_onSE;

        stk::mesh::EntityId owning_element_id = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
        stk::mesh::EntityRank owning_element_rank = subDimCellData.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

        if (DEBUG_NR_DEEP && subDimEntity.size() == 2 )
          {
            std::cout << "P[" << m_eMesh.get_rank() << "] unpacking edge, from_proc= " << from_proc << " with new node= " << m_eMesh.identifier(nodeIds_onSE[0])
                      << " buffer_entry= " << buffer_entry
                      << " new node id= " << nodeIds_onSE.m_entity_id_vector[0]
                      << " owning_element_id= " << owning_element_id << " owning_element_rank= " << owning_element_rank
                      << " subdim.size()= " << subDimEntity.size()
                      << " edge= " << m_eMesh.identifier(subDimEntity[0])
                      << " " << m_eMesh.identifier(subDimEntity[1])
                      << std::endl;
          }

        //VERIFY_OP(owning_element_id, !=, non_owning_elementId, "createNodeAndConnect:: bad elem ids");
        //VERIFY_OP(owning_element_id, < , non_owning_elementId, "createNodeAndConnect:: bad elem ids 2");

#ifndef NDEBUG
        // owning_elementId < non_owning_elementId && owning_elementRank >= non_owning_elementRank
        if (owning_element_rank == non_owning_elementRank)
          {
            if (owning_element_id == non_owning_elementId)
              {
                NR_PRINT(owning_element_id);
                NR_PRINT(non_owning_elementId);
                NR_PRINT(owning_element_rank);

                  {
                    std::cout << "P[" << m_eMesh.get_rank() << "] unpacking edge, from_proc= " << from_proc << " with new node= " << m_eMesh.identifier(nodeIds_onSE[0])
                              << " buffer_entry= " << buffer_entry
                              << " new node id= " << nodeIds_onSE.m_entity_id_vector[0]
                              << " owning_element_id= " << owning_element_id << " owning_element_rank= " << owning_element_rank
                              << " subdim.size()= " << subDimEntity.size()
                              << " edge= " << m_eMesh.identifier(subDimEntity[0])
                              << " " << m_eMesh.identifier(subDimEntity[1])
                              << std::endl;
                  }
              }
            VERIFY_OP(owning_element_id, !=, non_owning_elementId, "createNodeAndConnect:: bad elem ids");
            VERIFY_OP(owning_element_id, !=, non_owning_elementId, "createNodeAndConnect:: bad elem ids");
            VERIFY_OP(owning_element_id, < , non_owning_elementId, "createNodeAndConnect:: bad elem ids 2");
          }
#endif

      }

    public:
      SubDimCellToDataMap& getMap() { return  m_cell_2_data_map; }
      PerceptMesh& getMesh() { return m_eMesh; }
      bool getUseCustomGhosting() { return m_useCustomGhosting; }

      // remove any sub-dim entities from the map that have a node in deleted_nodes
      void cleanDeletedNodes(std::set<stk::mesh::Entity, stk::mesh::EntityLess>& deleted_nodes,
                             std::set<stk::mesh::Entity, stk::mesh::EntityLess>& kept_nodes_orig_minus_kept_nodes,
                             SubDimCellToDataMap& to_save,
//                              bool do_kept_nodes_orig,
                             bool debug=false)
      {
        std::set<stk::mesh::Entity, stk::mesh::EntityLess> deleted_nodes_copy = deleted_nodes;

        if (DEBUG_NR_UNREF)
          std::cout << "tmp cleanDeletedNodes deleted_nodes size: " << deleted_nodes_copy.size() << std::endl;

        SubDimCellToDataMap::iterator iter;
        std::vector<SubDimCellToDataMap::iterator> to_delete;

        SubDimCellToDataMap& map = m_cell_2_data_map;
        if (DEBUG_NR_UNREF)
          std::cout << "tmp cleanDeletedNodes map size: " << map.size() << std::endl;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();

            unsigned jj = 0;
            bool found = false;
            for (unsigned ii = 0; ii < nodeIds_onSE.size(); ii++)
              {
                if (deleted_nodes.find(nodeIds_onSE[ii]) != deleted_nodes.end())
                  {
                    if (kept_nodes_orig_minus_kept_nodes.find(nodeIds_onSE[ii]) != kept_nodes_orig_minus_kept_nodes.end())
                      {
                        to_save[subDimEntity] = nodeId_elementOwnderId;
                      }
                    found = true;
                    jj = ii;
                    deleted_nodes_copy.erase(nodeIds_onSE[ii]);
                    break;
                  }
              }
            if (found)
              {
                if (DEBUG_NR_UNREF)
                  {
                    std::cout << "tmp cleanDeletedNodes:: removing node id= " << m_eMesh.identifier(nodeIds_onSE[jj]) << std::endl;
                    std::cout << "Node: ";
                    m_eMesh.print_entity(std::cout, nodeIds_onSE[jj]);
                  }
                if (!debug)
                  {
                    //stk::mesh::EntityId owning_elementId = stk::mesh::entity_id(nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>());
                    //stk::mesh::EntityRank owning_elementRank = stk::mesh::entity_rank(nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>());
                    //nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>() = stk::mesh::EntityKey(0u, 0u);
                    //nodeIds_onSE.resize(0);
                    to_delete.push_back(iter);
                  }
              }
          }

        //std::cout << "tmp cleanDeletedNodes to_delete.size()= " << to_delete.size() << " map.size()= " << map.size() << std::endl;
        for (unsigned itd=0; itd < to_delete.size(); itd++)
          {
            map.erase(to_delete[itd]);
          }

        if (DEBUG_NR_UNREF && deleted_nodes_copy.size())
          {
            std::cout << "tmp cleanDeletedNodes some deleted nodes not found, size()=: " << deleted_nodes_copy.size() << " nodes= " << std::endl;
            std::set<stk::mesh::Entity, stk::mesh::EntityLess>::iterator it;
            for (it = deleted_nodes_copy.begin(); it != deleted_nodes_copy.end(); ++it)
              {
                stk::mesh::Entity node = *it;
                std::cout << "Node: ";
                m_eMesh.print_entity(std::cout, node);
              }

          }
      }

      // further cleanup of the NodeRegistry db - some elements get deleted on some procs but the ghost elements
      //   are still in the db - the easiest way to detect this is as used here: during modification_begin(),
      //   cleanup of cached transactions are performed, then we find these by seeing if our element id's are
      //   no longer in the stk_mesh db.

      void clear_element_owner_data_phase_2(bool resize_nodeId_data=true, bool mod_begin_end=true)
      {
        if (mod_begin_end)
          m_eMesh.get_bulk_data()->modification_begin();

        SubDimCellToDataMap::iterator iter;

        SubDimCellToDataMap& map = m_cell_2_data_map;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            stk::mesh::EntityRank owning_elementRank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

            if (owning_elementId)
              {
                //stk::mesh::Entity owning_element = get_entity_element(*m_eMesh.get_bulk_data(), owning_elementRank, owning_elementId);
                stk::mesh::Entity owning_element = m_eMesh.get_bulk_data()->get_entity(owning_elementRank, owning_elementId);

#if 0
                if (owning_element != owning_element_1)
                  {

                    std::cout << "P[" << m_eMesh.get_rank() << "] NR::clear_2 error # 1= " << owning_element << " " << owning_element_1 <<  std::endl;
                    throw std::logic_error("NodeRegistry:: clear_element_owner_data_phase_2 error # 1");
                  }
#endif

                if (!m_eMesh.is_valid(owning_element))
                  {
                    NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                    if (resize_nodeId_data) nodeIds_onSE.resize(0);

                    if (0)
                        std::cout << "clear_element_owner_data_phase_2 replacing " << owning_elementId << " rank= " << owning_elementRank << " with 0 "
                                  << " subdim.size()= " << subDimEntity.size() << " edge=" << m_eMesh.identifier(subDimEntity[0]) << " " << m_eMesh.identifier(subDimEntity[1])
                                  << std::endl;


                    nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>() = stk::mesh::EntityKey(owning_elementRank, 0u);

                  }

              }
            else
              {
                NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                if (resize_nodeId_data) nodeIds_onSE.resize(0);

                if (0 && !mod_begin_end) {
                  std::cout << "clear_element_owner_data_phase_2 error " << owning_elementId << " rank= " << owning_elementRank
                            << " subdim.size()= " << subDimEntity.size() << " edge=" << m_eMesh.identifier(subDimEntity[0]) << " " << m_eMesh.identifier(subDimEntity[1])
                            << std::endl;
                }


              }
          }
        if (mod_begin_end)
          m_eMesh.get_bulk_data()->modification_end();

      }

      // remove/zero any data that points to a deleted element
      // called by Refiner with "children_to_be_removed_with_ghosts"
      void clear_element_owner_data( std::set<stk::mesh::Entity, stk::mesh::EntityLess>& elems_to_be_deleted, bool resize_nodeId_data=true)
      {
        SubDimCellToDataMap::iterator iter;

        SubDimCellToDataMap& map = m_cell_2_data_map;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            stk::mesh::EntityId owning_elementId = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().id();
            stk::mesh::EntityRank owning_elementRank = nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>().rank();

            if (owning_elementId)
              {
                stk::mesh::Entity owning_element = get_entity_element(*m_eMesh.get_bulk_data(), owning_elementRank, owning_elementId);
                stk::mesh::Entity owning_element_1 = m_eMesh.get_bulk_data()->get_entity(owning_elementRank, owning_elementId);

                if (owning_element != owning_element_1)
                  {
                    std::cout << "P[" << m_eMesh.get_rank() << "] NR::clear_1 error # 1= " << owning_element << " " << owning_element_1 <<  std::endl;
                    throw std::logic_error("NodeRegistry:: clear_element_owner_data_phase_1 error # 1");
                  }

                if (m_eMesh.is_valid(owning_element))
                  {

                    bool isGhost = m_eMesh.isGhostElement(owning_element);

                    bool in_deleted_list = elems_to_be_deleted.find(owning_element) != elems_to_be_deleted.end();

                    if (in_deleted_list)
                      {

                        if (0)
                          std::cout << "clear_element_owner_data: owning_elementId = " << owning_elementId
                                    << " isGhost= " << isGhost << std::endl;

                        // FIXME
                        NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                        if (resize_nodeId_data) nodeIds_onSE.resize(0);
                        if (0)
                          std::cout << "clear_element_owner_data replacing " << owning_elementId << " rank= " << owning_elementRank << " with 0 "
                                  << " subdim.size()= " << subDimEntity.size() << " edge=" << m_eMesh.identifier(subDimEntity[0]) << " " << m_eMesh.identifier(subDimEntity[1])
                                  << std::endl;

                        // FIXME
                        nodeId_elementOwnderId.get<SDC_DATA_OWNING_ELEMENT_KEY>() = stk::mesh::EntityKey(owning_elementRank, 0u);
                      }
                  }
              }
            else
              {
                NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();
                if (resize_nodeId_data) nodeIds_onSE.resize(0);
              }
          }
      }

      void dumpDB(std::string msg="")
      {
        if (!DEBUG_NR_UNREF) return;
        SubDimCellToDataMap::iterator iter;
        SubDimCellToDataMap& map = m_cell_2_data_map;
        std::cout << msg << " tmp dumpDB map size: " << map.size() << std::endl;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();

            for (unsigned ii = 0; ii < nodeIds_onSE.size(); ii++)
              {
                std::cout << "tmp ddb:: node id= " << m_eMesh.identifier(nodeIds_onSE[ii]) << std::endl;
                std::cout << "subDimEntity= ";
                for (unsigned k=0; k < subDimEntity.size(); k++)
                  {
                    std::cout << " " << m_eMesh.identifier(subDimEntity[k]) << " ";
                  }
                std::cout << "Node: ";
                m_eMesh.print_entity(std::cout, nodeIds_onSE[ii]);
              }
          }
      }

      // estimate of memory used by this object
      unsigned get_memory_usage()
      {
        SubDimCellToDataMap::iterator iter;
        SubDimCellToDataMap& map = m_cell_2_data_map;

        unsigned mem=0;

        for (iter = map.begin(); iter != map.end(); ++iter)
          {
            const SubDimCell_SDCEntityType& subDimEntity = (*iter).first;
            SubDimCellData& nodeId_elementOwnderId = (*iter).second;

            mem += sizeof(SDCEntityType)*subDimEntity.size();
            mem += sizeof(stk::mesh::EntityKey);
            NodeIdsOnSubDimEntityType& nodeIds_onSE = nodeId_elementOwnderId.get<SDC_DATA_GLOBAL_NODE_IDS>();

            unsigned mem1 = (sizeof(NodeIdsOnSubDimEntityTypeQuantum)+
                             sizeof(stk::mesh::EntityId))*nodeIds_onSE.size() +sizeof(unsigned);

            mem += mem1;
          }
        return mem;
      }


    private:
      percept::PerceptMesh& m_eMesh;
      stk::CommAll * m_comm_all;
      SubDimCellToDataMap m_cell_2_data_map;

      vector<stk::mesh::EntityProc> m_nodes_to_ghost;
      SetOfEntities m_pseudo_entities;
      bool m_useCustomGhosting;

    public:
      int m_gee_cnt;
      int m_gen_cnt;
      //const CellTopologyData * const m_cell_topo_data;
      std::vector<EntityRepo> m_entity_repo;

      bool m_debug;

      NodeRegistryState m_state;
    };

  }
}
#endif
