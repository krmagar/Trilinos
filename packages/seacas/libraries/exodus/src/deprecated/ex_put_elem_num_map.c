/*
 * Copyright(C) 1999-2020 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 * 
 * See packages/seacas/LICENSE for details
 */

#include "exodusII.h"

/*!
\deprecated Use ex_put_id_map()(exoid, EX_ELEM_MAP, elem_map)

The function ex_put_elem_num_map() writes out the optional element
number map to the database. See Section LocalElementIds for a description
of the element number map. The function ex_put_init() must be invoked
before this call is made.

\return In case of an error, ex_put_elem_num_map() returns a negative
number; a warning will return a positive number. Possible causes of
errors include:
  -  data file not properly opened with call to ex_create() or ex_open()
  -  data file opened for read only.
  -  data file not initialized properly with call to ex_put_init().
  -  an element number map already exists in the file.

\param[in] exoid     exodus file ID returned from a previous call to ex_create()
or ex_open().
\param[in] elem_map  The element number map.

The following code generates a default element number map and outputs
it to an open exodus file. This is a trivial case and included just
for illustration. Since this map is optional, it should be written out
only if it contains something other than the default map.

~~~{.c}
int error, exoid;
int *elem_map = (int *)calloc(num_elem, sizeof(int));

for (i=1; i <= num_elem; i++)
   elem_map[i-1] = i;

error = ex_put_elem_num_map(exoid, elem_map);

\comment{Equivalent using non-deprecated function}
error = ex_put_id_map(exoid, EX_ELEM_MAP, elem_map);
~~~

 */

int ex_put_elem_num_map(int exoid, const void_int *elem_map)
{
  return ex_put_id_map(exoid, EX_ELEM_MAP, elem_map);
}
