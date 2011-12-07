// @HEADER
// ***********************************************************************
//
//                Copyright message goes here.   TODO
//
// ***********************************************************************
// @HEADER

/*! \file Zoltan2_Model.hpp

    \brief The abstract interface for a computational model.
*/


#ifndef _ZOLTAN2_MODEL_HPP_
#define _ZOLTAN2_MODEL_HPP_

#include <Zoltan2_Standards.hpp>
#include <Zoltan2_IdentifierMap.hpp>

namespace Zoltan2 {

/*! Zoltan2::Model
    to be called asynchronously.
*/
enum ModelType {
  InvalidModel = 0,
  HypergraphModelType,
  GraphModelType,
  GeometryModelType,
  IdentifierModelType
};


template <typename Adapter>
class Model
{
public:

   /*! Zoltan2 identifier types, user identifier types
    */
  typedef typename Adapter::lno_t    lno_t;
  typedef typename Adapter::gno_t    gno_t;
  typedef typename Adapter::lid_t    lid_t;
  typedef typename Adapter::gid_t    gid_t;
  typedef IdentifierMap<lid_t, gid_t, lno_t, gno_t> idmap_t;

  /*! Pure virtual destructor
   */
  virtual ~Model() {};

  /*! Constructor
   */
  Model() : idMap_() {}

   /*!
   *  Every model must have an IdentifierMap, whether it needs for mapping 
   *  or not. The Map can simply indicate that Zoltan2 global numbers are 
   *  identical to the application's global IDs.
   */
  const RCP<const idmap_t > getIdentifierMap() { return idMap_; }

  /*! Return the local number of objects, which may be
   *  vertices, matrix rows, identifiers, coordinates,
   *  or mesh nodes or elements.
   */
  virtual size_t getLocalNumObjects() const = 0;

  /*! Return the global number of objects, which may be
   *  vertices, matrix rows, identifiers, coordinates,
   *  or mesh nodes or elements.
   */
  virtual global_size_t getGlobalNumObjects() const = 0;

  /*! Set a view to the list of object global numbers, which may be
   *  vertex IDs, matrix row IDs, identifiers, coordinate IDs,
   *  or mesh node or element IDs.
   */
  virtual void getGlobalObjectIds(ArrayView<const gno_t> &gnos) const = 0;

protected:

  /*! Set the IdentifierMap used by the model.
   */
  void setIdentifierMap(RCP<const idmap_t> &map) { idMap_ = map; }

private:

  RCP<const idmap_t> idMap_;
};

}

#endif
