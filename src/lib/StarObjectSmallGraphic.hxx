/* -*- Mode: C++; c-default-style: "k&r"; indent-tabs-mode: nil; tab-width: 2; c-basic-offset: 2 -*- */

/* libstaroffice
* Version: MPL 2.0 / LGPLv2+
*
* The contents of this file are subject to the Mozilla Public License Version
* 2.0 (the "License"); you may not use this file except in compliance with
* the License or as specified alternatively below. You may obtain a copy of
* the License at http://www.mozilla.org/MPL/
*
* Software distributed under the License is distributed on an "AS IS" basis,
* WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
* for the specific language governing rights and limitations under the
* License.
*
* Major Contributor(s):
* Copyright (C) 2002 William Lachance (wrlach@gmail.com)
* Copyright (C) 2002,2004 Marc Maurer (uwog@uwog.net)
* Copyright (C) 2004-2006 Fridrich Strba (fridrich.strba@bluewin.ch)
* Copyright (C) 2006, 2007 Andrew Ziem
* Copyright (C) 2011, 2012 Alonso Laurent (alonso@loria.fr)
*
*
* All Rights Reserved.
*
* For minor contributions see the git repository.
*
* Alternatively, the contents of this file may be used under the terms of
* the GNU Lesser General Public License Version 2 or later (the "LGPLv2+"),
* in which case the provisions of the LGPLv2+ are applicable
* instead of those above.
*/

/*
 * Parser to convert a small graphic zone/OLE in a StarOffice document
 *
 */
#ifndef STAR_OBJECT_SMALL_GRAPHIC
#  define STAR_OBJECT_SMALL_GRAPHIC

#include <vector>

#include "libstaroffice_internal.hxx"
#include "StarObject.hxx"

namespace StarObjectSmallGraphicInternal
{
struct State;
}

class StarZone;

/** \brief the main class to read a small StarOffice graphic zone
 *
 *
 *
 */
class StarObjectSmallGraphic : public StarObject
{
public:
  //! constructor
  StarObjectSmallGraphic(StarObject const &orig, bool duplicateState);
  //! destructor
  virtual ~StarObjectSmallGraphic();
  //! try to read a object zone: "DrOb'
  bool readSdrObject(StarZone &zone);

protected:
  //
  // low level
  //
  //! try to read a SVDr object
  bool readSVDRObject(StarZone &zone, int identifier);
  //! try to read the first zone of SVDr object
  bool readSVDRObjectHeader(StarZone &zone);
  //! try to read the object attrib zone
  bool readSVDRObjectAttrib(StarZone &zone);
  //! try to read the object text zone
  bool readSVDRObjectText(StarZone &zone);
  //! try to read the object rect zone
  bool readSVDRObjectRect(StarZone &zone, int identifier);
  //! try to read the object caption zone
  bool readSVDRObjectCaption(StarZone &zone);
  //! try to read the object circle zone
  bool readSVDRObjectCircle(StarZone &zone, int identifier);
  //! try to read the object edge zone
  bool readSVDRObjectEdge(StarZone &zone);
  //! try to read the object graph zone
  bool readSVDRObjectGraph(StarZone &zone);
  //! try to read the object group zone
  bool readSVDRObjectGroup(StarZone &zone);
  //! try to read the object measure zone
  bool readSVDRObjectMeasure(StarZone &zone);
  //! try to read the object OLE zone
  bool readSVDRObjectOLE(StarZone &zone, int identifier);
  //! try to read the object path zone
  bool readSVDRObjectPath(StarZone &zone, int identifier);

  //! try to read a SDR gluepoint
  bool readSDRGluePoint(StarZone &zone);
  //! try to read a SDR gluepoint list
  bool readSDRGluePointList(StarZone &zone);
  //! try to read a SDR object connection
  bool readSDRObjectConnection(StarZone &zone);
  //! try to read a SDR object surrogate
  bool readSDRObjectSurrogate(StarZone &zone);
  //! try to read a outliner para object
  bool readSDROutlinerParaObject(StarZone &zone);
  //! try to read a SDR userData
  bool readSDRUserData(StarZone &zone, bool inRecord);
  //! try to read a SDR userData list
  bool readSDRUserDataList(StarZone &zone, bool inRecord);

  //! try to read a SCHU object
  bool readSCHUObject(StarZone &zone, int identifier);

  //! try to read a SDDU object
  bool readSDUDObject(StarZone &zone, int identifier);

protected:
  //
  // data
  //

  //! the state
  shared_ptr<StarObjectSmallGraphicInternal::State> m_graphicState;
private:
  StarObjectSmallGraphic &operator=(StarObjectSmallGraphic const &orig);
};
#endif
// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
