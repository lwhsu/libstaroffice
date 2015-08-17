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

#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

#include <librevenge/librevenge.h>

#include "StarAttribute.hxx"
#include "StarFileManager.hxx"
#include "StarDocument.hxx"
#include "StarZone.hxx"

#include "StarItemPool.hxx"

/** Internal: the structures of a StarItemPool */
namespace StarItemPoolInternal
{
////////////////////////////////////////
//! Internal: a structure use to read SfxMultiRecord zone of a StarItemPool
struct SfxMultiRecord {
  //! constructor
  SfxMultiRecord() : m_zone(0), m_zoneType(0), m_zoneOpened(false), m_headerType(0), m_headerVersion(0), m_headerTag(0),
    m_actualRecord(0), m_numRecord(0), m_contentSize(0),
    m_startPos(0), m_endPos(0), m_offsetList(), m_extra("")
  {
  }
  //! returns true if the record is opened
  bool isOpened() const
  {
    return m_zoneOpened;
  }
  //! returns the number of record
  uint16_t getNumRecords() const
  {
    return m_zoneOpened ? m_numRecord : 0;
  }
  //! returns the header tag or -1
  int getHeaderTag() const
  {
    return !m_zoneOpened ? -1 : int(m_headerTag);
  }
  //! try to open a zone
  bool open(StarZone &zone)
  {
    if (m_zoneOpened) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord: oops a record has been opened\n"));
      return false;
    }
    m_actualRecord=m_numRecord=0;
    m_headerType=m_headerVersion=0;
    m_headerTag=0;
    m_contentSize=0;
    m_offsetList.clear();
    m_zone=&zone;
    STOFFInputStreamPtr input=m_zone->input();
    long pos=input->tell();
    if (!m_zone->openSfxRecord(m_zoneType)) {
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }
    if (m_zoneType==char(0xff)) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord: oops end header\n"));
      m_extra="###emptyZone,";
      return true; /* empty zone*/
    }
    if (m_zoneType!=0) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord: find unknown header\n"));
      m_extra="###badZoneType,";
      return true;
    }

    m_zoneOpened=true;
    m_endPos=m_zone->getRecordLastPosition();
    // filerec.cxx: SfxSingleRecordReader::FindHeader_Impl
    if (input->tell()+10>m_endPos) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::open: oops the zone seems too short\n"));
      m_extra="###zoneShort,";
      return true;
    }
    *input >> m_headerType >> m_headerVersion >> m_headerTag;
    // filerec.cxx: SfxMultiRecordReader::ReadHeader_Impl
    *input >> m_numRecord >> m_contentSize;
    m_startPos=input->tell();
    std::stringstream s;
    if (m_headerType==2) {
      // fixed size
      if (m_startPos+long(m_numRecord)*long(m_contentSize) > m_endPos) {
        STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::open: oops the number of record seems bad\n"));
        s << "##numRecord=" << m_numRecord << ",";
        if (m_contentSize && m_endPos>m_startPos)
          m_numRecord=uint16_t((m_endPos-m_startPos)/long(m_contentSize));
        else
          m_numRecord=0;
      }
      m_extra=s.str();
      return true;
    }

    long debOffsetList=((m_headerType==3 || m_headerType==7) ? m_startPos : 0) + long(m_contentSize);
    if (debOffsetList<m_startPos || debOffsetList+4*m_numRecord > m_endPos) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::open: can not find the version map offset\n"));
      s << "###contentCount";
      m_numRecord=0;
      m_extra=s.str();
      return true;
    }
    m_endPos=debOffsetList;
    input->seek(debOffsetList, librevenge::RVNG_SEEK_SET);
    for (uint16_t i=0; i<m_numRecord; ++i) {
      uint32_t offset;
      *input >> offset;
      m_offsetList.push_back(offset);
    }
    input->seek(m_startPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  //! try to close a zone
  void close(std::string const &wh)
  {
    if (!m_zone) return;
    if (!m_zoneOpened) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::close: can not find any opened zone\n"));
      return;
    }
    m_zoneOpened=false;
    STOFFInputStreamPtr input=m_zone->input();
    if (input->tell()<m_endPos && input->tell()+4>=m_endPos) { // small diff is possible
      m_zone->ascii().addDelimiter(input->tell(),'|');
      input->seek(m_zone->getRecordLastPosition(), librevenge::RVNG_SEEK_SET);
    }
    else if (input->tell()==m_endPos)
      input->seek(m_zone->getRecordLastPosition(), librevenge::RVNG_SEEK_SET);
    m_zone->closeSfxRecord(m_zoneType, wh);
    m_zone=0;
  }
  //! try to go to the new content positon
  bool getNewContent(std::string const &wh)
  {
    if (!m_zone) return false;
    // SfxMultiRecordReader::GetContent
    long newPos=getLastContentPosition();
    if (newPos>=m_endPos) return false;
    STOFFInputStreamPtr input=m_zone->input();
    ++m_actualRecord;
    if (input->tell()<newPos && input->tell()+4>=newPos) { // small diff is possible
      m_zone->ascii().addDelimiter(input->tell(),'|');
      input->seek(newPos, librevenge::RVNG_SEEK_SET);
    }
    else if (input->tell()!=newPos) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::getNewContent: find extra data\n"));
      m_zone->ascii().addPos(input->tell());
      libstoff::DebugStream f;
      f << wh << ":###extra";
      m_zone->ascii().addNote(f.str().c_str());
      input->seek(newPos, librevenge::RVNG_SEEK_SET);
    }
    if (m_headerType==7 || m_headerType==8) {
      // TODO: readtag
      input->seek(2, librevenge::RVNG_SEEK_CUR);
    }
    return true;
  }
  //! returns the last content position
  long getLastContentPosition() const
  {
    if (m_actualRecord >= m_numRecord) return m_endPos;
    if (m_headerType==2) return m_startPos+m_actualRecord*(long)m_contentSize;
    if (m_actualRecord >= uint16_t(m_offsetList.size())) {
      STOFF_DEBUG_MSG(("StarItemPoolInternal::SfxMultiRecord::getLastContentPosition: argh, find unexpected index\n"));
      return m_endPos;
    }
    return m_startPos+long(m_offsetList[size_t(m_actualRecord)]>>8)-14;
  }

  //! basic operator<< ; print header data
  friend std::ostream &operator<<(std::ostream &o, SfxMultiRecord const &r)
  {
    if (!r.m_zoneOpened) {
      o << r.m_extra;
      return o;
    }
    if (r.m_headerType) o << "type=" << int(r.m_headerType) << ",";
    if (r.m_headerVersion) o << "version=" << int(r.m_headerVersion) << ",";
    if (r.m_headerTag) o << "tag=" << r.m_headerTag << ",";
    if (r.m_numRecord) o << "num[record]=" << r.m_numRecord << ",";
    if (r.m_contentSize) o << "content[size/pos]=" << r.m_contentSize << ",";
    if (!r.m_offsetList.empty()) {
      o << "offset=[";
      for (size_t i=0; i<r.m_offsetList.size(); ++i) {
        uint32_t off=r.m_offsetList[i];
        if (off&0xff)
          o << (off>>8) << ":" << (off&0xff) << ",";
        else
          o << (off>>8) << ",";
      }
      o << "],";
    }
    o << r.m_extra;
    return o;
  }
protected:
  //! the main zone
  StarZone *m_zone;
  //! the zone type
  char m_zoneType;
  //! true if a SfxRecord has been opened
  bool m_zoneOpened;
  //! the record type
  uint8_t m_headerType;
  //! the header version
  uint8_t m_headerVersion;
  //! the header tag
  uint16_t m_headerTag;
  //! the actual record
  uint16_t m_actualRecord;
  //! the number of record
  uint16_t m_numRecord;
  //! the record/content/pos size
  uint32_t m_contentSize;
  //! the start of data position
  long m_startPos;
  //! the end of data position
  long m_endPos;
  //! the list of (offset + type)
  std::vector<uint32_t> m_offsetList;
  //! extra data
  std::string m_extra;
private:
  SfxMultiRecord(SfxMultiRecord const &orig);
  SfxMultiRecord &operator=(SfxMultiRecord const &orig);
};

//! small struct used to keep a list of version
struct Version {
  //! constructor
  Version(int vers, int start, std::vector<int> const &list) :
    m_version(vers), m_start(start), m_list(list), m_invertListMap()
  {
    for (size_t i=0; i<m_list.size(); ++i)
      m_invertListMap[list[i]]=int(i);
  }
  //! the version number
  int m_version;
  //! int the start value
  int m_start;
  //! the list of value
  std::vector<int> m_list;
  //! a map offset to which
  std::map<int,int> m_invertListMap;
};

////////////////////////////////////////
//! Internal: the state of a StarItemPool
struct State {
  //! constructor
  State(StarDocument &document) : m_document(document), m_majorVersion(0), m_minorVersion(0), m_loadingVersion(0), m_name(""),
    m_currentVersion(0), m_verStart(0), m_verEnd(0), m_versionList(), m_idToAttributeList()
  {
  }

  //! set the pool name
  void setPoolName(librevenge::RVNGString const &name)
  {
    m_name=name;
    init();
  }
  //! the pool
  void init()
  {
    // to do VCControls
    if (m_name=="EditEngineItemPool") {
      m_verStart=3989;
      m_verEnd=4037;
      // svx_editdoc.cxx
      std::vector<int> list;
      for (int i = 0; i <= 14; ++i) list.push_back(3999+i);
      for (int i = 15; i <= 17; ++i) list.push_back(3999+i+4);
      addVersionMap(1, 3999, list);

      list.clear();
      for (int i = 0; i <= 17; ++i) list.push_back(3999+i);
      for (int i=18; i<=20; ++i)  list.push_back(3999+i+1);
      addVersionMap(2, 3999, list);

      list.clear();
      for (int i = 0; i <= 10; ++i) list.push_back(3997+i);
      for (int i=11; i<=23; ++i)  list.push_back(3997+i+1);
      addVersionMap(3, 3997, list);

      list.clear();
      for (int i = 0; i <= 24; ++i) list.push_back(3994+i);
      for (int i=25; i<=28; ++i)  list.push_back(3994+i+15);
      addVersionMap(4, 3994, list);
      // svx_eerdll.cxx GlobalEditData::GetDefItems
      static int const(what[])= {
        StarAttribute::ATTR_SC_WRITINGDIR, StarAttribute::ATTR_EE_PARA_XMLATTRIBS, StarAttribute::ATTR_PARA_HANGINGPUNCTUATION, StarAttribute::ATTR_PARA_FORBIDDEN_RULES,
        StarAttribute::ATTR_EE_PARA_ASIANCJKSPACING, StarAttribute::ATTR_EE_PARA_NUMBULLET, StarAttribute::ATTR_SC_HYPHENATE, StarAttribute::ATTR_EE_PARA_BULLETSTATE,
        StarAttribute::ATTR_EE_PARA_OUTLLR_SPACE, StarAttribute::ATTR_EE_PARA_OUTLLEVEL, StarAttribute::ATTR_EE_PARA_BULLET, StarAttribute::ATTR_FRM_LR_SPACE,
        StarAttribute::ATTR_FRM_UL_SPACE, StarAttribute::ATTR_PARA_LINESPACING, StarAttribute::ATTR_PARA_ADJUST, StarAttribute::ATTR_PARA_TABSTOP,
        StarAttribute::ATTR_CHR_COLOR, StarAttribute::ATTR_CHR_FONT, StarAttribute::ATTR_CHR_FONTSIZE, StarAttribute::ATTR_EE_CHR_SCALEW,

        StarAttribute::ATTR_CHR_WEIGHT, StarAttribute::ATTR_CHR_UNDERLINE, StarAttribute::ATTR_CHR_CROSSEDOUT, StarAttribute::ATTR_CHR_POSTURE,
        StarAttribute::ATTR_CHR_CONTOUR, StarAttribute::ATTR_CHR_SHADOWED, StarAttribute::ATTR_CHR_ESCAPEMENT, StarAttribute::ATTR_CHR_AUTOKERN,
        StarAttribute::ATTR_CHR_KERNING, StarAttribute::ATTR_CHR_WORDLINEMODE, StarAttribute::ATTR_CHR_LANGUAGE, StarAttribute::ATTR_CHR_CJK_LANGUAGE,
        StarAttribute::ATTR_CHR_CTL_LANGUAGE, StarAttribute::ATTR_CHR_CJK_FONT, StarAttribute::ATTR_CHR_CTL_FONT, StarAttribute::ATTR_CHR_CJK_FONTSIZE,
        StarAttribute::ATTR_CHR_CTL_FONTSIZE, StarAttribute::ATTR_CHR_CJK_WEIGHT, StarAttribute::ATTR_CHR_CTL_WEIGHT, StarAttribute::ATTR_CHR_CJK_POSTURE,

        StarAttribute::ATTR_CHR_CTL_POSTURE, StarAttribute::ATTR_CHR_EMPHASIS_MARK, StarAttribute::ATTR_CHR_RELIEF, StarAttribute::ATTR_EE_CHR_RUBI_DUMMY,
        StarAttribute::ATTR_EE_CHR_XMLATTRIBS, StarAttribute::ATTR_EE_FEATURE_TAB, StarAttribute::ATTR_EE_FEATURE_LINEBR, StarAttribute::ATTR_CHR_CHARSETCOLOR,
        StarAttribute::ATTR_EE_FEATURE_FIELD
      };
      for (int i=0; i<int(sizeof(what)/sizeof(int)); ++i)
        m_idToAttributeList.push_back(what[i]);
    }
    else if (m_name=="SchItemPool") {
      // sch_itempool.cxx SchItemPool::SchItemPool
      m_verStart=1; // SCHATTR_START
      m_verEnd=100; // SCHATTR_NONPERSISTENT_START
      // svx_eerdll.cxx GlobalEditData::GetDefItems
      static int const(what[])= {
        StarAttribute::ATTR_SCH_DATADESCR_DESCR, StarAttribute::ATTR_SCH_DATADESCR_SHOW_SYM, StarAttribute::ATTR_SCH_LEGEND_POS, StarAttribute::ATTR_SCH_TEXT_ORIENT,
        StarAttribute::ATTR_SCH_TEXT_ORDER, StarAttribute::ATTR_SCH_Y_AXIS_AUTO_MIN, StarAttribute::ATTR_SCH_Y_AXIS_MIN, StarAttribute::ATTR_SCH_Y_AXIS_AUTO_MAX,
        StarAttribute::ATTR_SCH_Y_AXIS_MAX, StarAttribute::ATTR_SCH_Y_AXIS_AUTO_STEP_MAIN, StarAttribute::ATTR_SCH_Y_AXIS_STEP_MAIN, StarAttribute::ATTR_SCH_Y_AXIS_AUTO_STEP_HELP,
        StarAttribute::ATTR_SCH_Y_AXIS_STEP_HELP, StarAttribute::ATTR_SCH_Y_AXIS_LOGARITHM, StarAttribute::ATTR_SCH_Y_AXIS_AUTO_ORIGIN, StarAttribute::ATTR_SCH_Y_AXIS_ORIGIN,
        StarAttribute::ATTR_SCH_X_AXIS_AUTO_MIN, StarAttribute::ATTR_SCH_X_AXIS_MIN, StarAttribute::ATTR_SCH_X_AXIS_AUTO_MAX, StarAttribute::ATTR_SCH_X_AXIS_MAX,

        StarAttribute::ATTR_SCH_X_AXIS_AUTO_STEP_MAIN, StarAttribute::ATTR_SCH_X_AXIS_STEP_MAIN, StarAttribute::ATTR_SCH_X_AXIS_AUTO_STEP_HELP, StarAttribute::ATTR_SCH_X_AXIS_STEP_HELP,
        StarAttribute::ATTR_SCH_X_AXIS_LOGARITHM, StarAttribute::ATTR_SCH_X_AXIS_AUTO_ORIGIN, StarAttribute::ATTR_SCH_X_AXIS_ORIGIN, StarAttribute::ATTR_SCH_Z_AXIS_AUTO_MIN,
        StarAttribute::ATTR_SCH_Z_AXIS_MIN, StarAttribute::ATTR_SCH_Z_AXIS_AUTO_MAX, StarAttribute::ATTR_SCH_Z_AXIS_MAX, StarAttribute::ATTR_SCH_Z_AXIS_AUTO_STEP_MAIN,
        StarAttribute::ATTR_SCH_Z_AXIS_STEP_MAIN, StarAttribute::ATTR_SCH_Z_AXIS_AUTO_STEP_HELP, StarAttribute::ATTR_SCH_Z_AXIS_STEP_HELP, StarAttribute::ATTR_SCH_Z_AXIS_LOGARITHM,
        StarAttribute::ATTR_SCH_Z_AXIS_AUTO_ORIGIN, StarAttribute::ATTR_SCH_Z_AXIS_ORIGIN, StarAttribute::ATTR_SCH_AXISTYPE, StarAttribute::ATTR_SCH_DUMMY0,

        StarAttribute::ATTR_SCH_DUMMY1, StarAttribute::ATTR_SCH_DUMMY2, StarAttribute::ATTR_SCH_DUMMY3, StarAttribute::ATTR_SCH_DUMMY_END,
        StarAttribute::ATTR_SCH_STAT_AVERAGE, StarAttribute::ATTR_SCH_STAT_KIND_ERROR, StarAttribute::ATTR_SCH_STAT_PERCENT, StarAttribute::ATTR_SCH_STAT_BIGERROR,
        StarAttribute::ATTR_SCH_STAT_CONSTPLUS, StarAttribute::ATTR_SCH_STAT_CONSTMINUS, StarAttribute::ATTR_SCH_STAT_REGRESSTYPE, StarAttribute::ATTR_SCH_STAT_INDICATE,
        StarAttribute::ATTR_SCH_TEXT_DEGREES, StarAttribute::ATTR_SCH_TEXT_OVERLAP, StarAttribute::ATTR_SCH_TEXT_DUMMY0, StarAttribute::ATTR_SCH_TEXT_DUMMY1,
        StarAttribute::ATTR_SCH_TEXT_DUMMY2, StarAttribute::ATTR_SCH_TEXT_DUMMY3, StarAttribute::ATTR_SCH_STYLE_DEEP, StarAttribute::ATTR_SCH_STYLE_3D,

        StarAttribute::ATTR_SCH_STYLE_VERTICAL, StarAttribute::ATTR_SCH_STYLE_BASETYPE, StarAttribute::ATTR_SCH_STYLE_LINES, StarAttribute::ATTR_SCH_STYLE_PERCENT,
        StarAttribute::ATTR_SCH_STYLE_STACKED, StarAttribute::ATTR_SCH_STYLE_SPLINES, StarAttribute::ATTR_SCH_STYLE_SYMBOL, StarAttribute::ATTR_SCH_STYLE_SHAPE,
        StarAttribute::ATTR_SCH_AXIS, StarAttribute::ATTR_SCH_AXIS_AUTO_MIN, StarAttribute::ATTR_SCH_AXIS_MIN, StarAttribute::ATTR_SCH_AXIS_AUTO_MAX,
        StarAttribute::ATTR_SCH_AXIS_MAX, StarAttribute::ATTR_SCH_AXIS_AUTO_STEP_MAIN, StarAttribute::ATTR_SCH_AXIS_STEP_MAIN, StarAttribute::ATTR_SCH_AXIS_AUTO_STEP_HELP,
        StarAttribute::ATTR_SCH_AXIS_STEP_HELP, StarAttribute::ATTR_SCH_AXIS_LOGARITHM, StarAttribute::ATTR_SCH_AXIS_AUTO_ORIGIN, StarAttribute::ATTR_SCH_AXIS_ORIGIN,

        StarAttribute::ATTR_SCH_AXIS_TICKS, StarAttribute::ATTR_SCH_AXIS_NUMFMT, StarAttribute::ATTR_SCH_AXIS_NUMFMTPERCENT, StarAttribute::ATTR_SCH_AXIS_SHOWAXIS,
        StarAttribute::ATTR_SCH_AXIS_SHOWDESCR, StarAttribute::ATTR_SCH_AXIS_SHOWMAINGRID, StarAttribute::ATTR_SCH_AXIS_SHOWHELPGRID, StarAttribute::ATTR_SCH_AXIS_TOPDOWN,
        StarAttribute::ATTR_SCH_AXIS_HELPTICKS, StarAttribute::ATTR_SCH_AXIS_DUMMY0, StarAttribute::ATTR_SCH_AXIS_DUMMY1, StarAttribute::ATTR_SCH_AXIS_DUMMY2,
        StarAttribute::ATTR_SCH_AXIS_DUMMY3, StarAttribute::ATTR_SCH_BAR_OVERLAP, StarAttribute::ATTR_SCH_BAR_GAPWIDTH, StarAttribute::ATTR_SCH_SYMBOL_BRUSH,
        StarAttribute::ATTR_SCH_STOCK_VOLUME, StarAttribute::ATTR_SCH_STOCK_UPDOWN, StarAttribute::ATTR_SCH_SYMBOL_SIZE, StarAttribute::ATTR_SCH_USER_DEFINED_ATTR
      };

      for (int i=0; i<int(sizeof(what)/sizeof(int)); ++i)
        m_idToAttributeList.push_back(what[i]);
    }
    else if (m_name=="ScDocumentPool") {
      // sc_docpool.cxx
      m_verStart=100; // ATTR_STARTINDEX
      m_verEnd=183; // ATTR_ENDINDEX

      std::vector<int> list;
      for (int i = 0; i <= 17; i++) list.push_back(100+i);
      for (int i = 18; i <= 57; i++) list.push_back(100+i+1);
      addVersionMap(1, 100, list);

      list.clear();
      for (int i = 0; i <= 23; i++) list.push_back(100+i);
      for (int i = 24; i <= 58; i++) list.push_back(100+i+2);
      addVersionMap(2, 100, list);

      list.clear();
      for (int i = 0; i <= 10; i++) list.push_back(100+i);
      for (int i = 11; i <= 60; i++) list.push_back(100+i+1);
      addVersionMap(3, 100, list);

      list.clear();
      for (int i = 0; i <= 13; i++) list.push_back(100+i);
      for (int i = 14; i <= 61; i++) list.push_back(100+i+2);
      addVersionMap(4, 100, list);

      list.clear();
      for (int i = 0; i <= 9; i++) list.push_back(100+i);
      for (int i = 10; i <= 63; i++) list.push_back(100+i+12);
      addVersionMap(5, 100, list);

      list.clear();
      for (int i = 0; i <= 21; i++) list.push_back(100+i);
      for (int i = 22; i <= 75; i++) list.push_back(100+i+3);
      addVersionMap(6, 100, list);

      list.clear();
      for (int i = 0; i <= 21; i++) list.push_back(100+i);
      for (int i = 22; i <= 78; i++) list.push_back(100+i+3);
      addVersionMap(7, 100, list);

      list.clear();
      for (int i = 0; i <= 33; i++) list.push_back(100+i);
      for (int i = 34; i <= 81; i++) list.push_back(100+i+1);
      addVersionMap(8, 100, list);

      list.clear();
      for (int i = 0; i <= 34; i++) list.push_back(100+i);
      for (int i = 35; i <= 82; i++) list.push_back(100+i+1);
      addVersionMap(9, 100, list);
      static int const(what[])= {
        StarAttribute::ATTR_CHR_FONT, StarAttribute::ATTR_CHR_FONTSIZE, StarAttribute::ATTR_CHR_WEIGHT, StarAttribute::ATTR_CHR_POSTURE,
        StarAttribute::ATTR_CHR_UNDERLINE, StarAttribute::ATTR_CHR_CROSSEDOUT, StarAttribute::ATTR_CHR_CONTOUR, StarAttribute::ATTR_CHR_SHADOWED,
        StarAttribute::ATTR_CHR_COLOR, StarAttribute::ATTR_CHR_LANGUAGE, StarAttribute::ATTR_CHR_CJK_FONT, StarAttribute::ATTR_CHR_CJK_FONTSIZE,
        StarAttribute::ATTR_CHR_CJK_WEIGHT, StarAttribute::ATTR_CHR_CJK_POSTURE, StarAttribute::ATTR_CHR_CJK_LANGUAGE, StarAttribute::ATTR_CHR_CTL_FONT,
        StarAttribute::ATTR_CHR_CTL_FONTSIZE, StarAttribute::ATTR_CHR_CTL_WEIGHT, StarAttribute::ATTR_CHR_CTL_POSTURE, StarAttribute::ATTR_CHR_CTL_LANGUAGE,

        StarAttribute::ATTR_CHR_EMPHASIS_MARK, StarAttribute::ATTR_SC_USERDEF, StarAttribute::ATTR_CHR_WORDLINEMODE, StarAttribute::ATTR_CHR_RELIEF,
        StarAttribute::ATTR_SC_HYPHENATE, StarAttribute::ATTR_PARA_SCRIPTSPACE, StarAttribute::ATTR_PARA_HANGINGPUNCTUATION, StarAttribute::ATTR_PARA_FORBIDDEN_RULES,
        StarAttribute::ATTR_SC_HORJUSTIFY, StarAttribute::ATTR_SC_INDENT, StarAttribute::ATTR_SC_VERJUSTIFY, StarAttribute::ATTR_SC_ORIENTATION,
        StarAttribute::ATTR_SC_ROTATE_VALUE, StarAttribute::ATTR_SC_ROTATE_MODE, StarAttribute::ATTR_SC_VERTICAL_ASIAN, StarAttribute::ATTR_SC_WRITINGDIR,
        StarAttribute::ATTR_SC_LINEBREAK, StarAttribute::ATTR_SC_MARGIN, StarAttribute::ATTR_SC_MERGE, StarAttribute::ATTR_SC_MERGE_FLAG,

        StarAttribute::ATTR_SC_VALUE_FORMAT, StarAttribute::ATTR_SC_LANGUAGE_FORMAT, StarAttribute::ATTR_FRM_BACKGROUND, StarAttribute::ATTR_SC_PROTECTION,
        StarAttribute::ATTR_SC_BORDER, StarAttribute::ATTR_SC_BORDER_INNER, StarAttribute::ATTR_FRM_SHADOW, StarAttribute::ATTR_SC_VALIDDATA,
        StarAttribute::ATTR_SC_CONDITIONAL, StarAttribute::ATTR_SC_PATTERN, StarAttribute::ATTR_FRM_LR_SPACE, StarAttribute::ATTR_FRM_UL_SPACE,
        StarAttribute::ATTR_SC_PAGE, StarAttribute::ATTR_SC_PAGE_PAPERTRAY, StarAttribute::ATTR_FRM_PAPER_BIN, StarAttribute::ATTR_SC_PAGE_SIZE,
        StarAttribute::ATTR_SC_PAGE_MAXSIZE, StarAttribute::ATTR_SC_PAGE_HORCENTER, StarAttribute::ATTR_SC_PAGE_VERCENTER, StarAttribute::ATTR_SC_PAGE_ON,

        StarAttribute::ATTR_SC_PAGE_DYNAMIC, StarAttribute::ATTR_SC_PAGE_SHARED, StarAttribute::ATTR_SC_PAGE_NOTES, StarAttribute::ATTR_SC_PAGE_GRID,
        StarAttribute::ATTR_SC_PAGE_HEADERS, StarAttribute::ATTR_SC_PAGE_CHARTS, StarAttribute::ATTR_SC_PAGE_OBJECTS, StarAttribute::ATTR_SC_PAGE_DRAWINGS,
        StarAttribute::ATTR_SC_PAGE_TOPDOWN, StarAttribute::ATTR_SC_PAGE_SCALE, StarAttribute::ATTR_SC_PAGE_SCALETOPAGES, StarAttribute::ATTR_SC_PAGE_FIRSTPAGENO,
        StarAttribute::ATTR_SC_PAGE_PRINTAREA, StarAttribute::ATTR_SC_PAGE_REPEATROW, StarAttribute::ATTR_SC_PAGE_REPEATCOL, StarAttribute::ATTR_SC_PAGE_PRINTTABLES,
        StarAttribute::ATTR_SC_PAGE_HEADERLEFT, StarAttribute::ATTR_SC_PAGE_FOOTERLEFT, StarAttribute::ATTR_SC_PAGE_HEADERRIGHT, StarAttribute::ATTR_SC_PAGE_FOOTERRIGHT,
        StarAttribute::ATTR_SC_PAGE_HEADERSET, StarAttribute::ATTR_SC_PAGE_FOOTERSET, StarAttribute::ATTR_SC_PAGE_FORMULAS, StarAttribute::ATTR_SC_PAGE_NULLVALS
      };
      for (int i=0; i<int(sizeof(what)/sizeof(int)); ++i)
        m_idToAttributeList.push_back(what[i]);
    }
    else if (m_name=="SWG") {
      // SwAttrPool::SwAttrPool set default map
      m_verStart=1; //POOLATTR_BEGIN
      m_verEnd=130; //POOLATTR_END-1
      for (int i=StarAttribute::ATTR_CHR_CASEMAP; i<=StarAttribute::ATTR_BOX_VALUE; ++i)
        m_idToAttributeList.push_back(i);
      std::vector<int> list;
      // sw_swatrset.cxx SwAttrPool::SwAttrPool and sw_init.cxx pVersionMap1
      for (int i = 1; i <= 17; i++) list.push_back(i);
      for (int i = 18; i <= 27; i++) list.push_back(i+5);
      for (int i = 28; i <= 35; i++) list.push_back(i+7);
      for (int i = 36; i <= 58; i++) list.push_back(i+10);
      for (int i = 59; i <= 60; i++) list.push_back(i+12);
      addVersionMap(1, 1, list);
      list.clear();
      for (int i = 1; i <= 70; i++) list.push_back(i);
      for (int i = 71; i <= 75; i++) list.push_back(i+10);
      addVersionMap(2, 1, list);
      list.clear();
      for (int i = 1; i <= 21; i++) list.push_back(i);
      for (int i = 22; i <= 27; i++) list.push_back(i+15);
      for (int i = 28; i <= 82; i++) list.push_back(i+20);
      for (int i = 83; i <= 86; i++) list.push_back(i+35);
      addVersionMap(3, 1, list);
      list.clear();
      for (int i = 1; i <= 65; i++) list.push_back(i);
      for (int i = 66; i <= 121; i++) list.push_back(i+9);
      addVersionMap(4, 1, list);
    }
    else if (m_name=="XOutdevItemPool") {
      // svx_xpool.cxx XOutdevItemPool::Ctor
      m_verStart=1000;
      m_verEnd=1228; // checkme
      std::vector<int> list;
      for (int i=1000; i<=1021; ++i) list.push_back(i);
      for (int i=1022; i<=1039; ++i) list.push_back(i+13);
      addVersionMap(1, 1000, list);

      list.clear();
      for (int i=1000; i<=1009; ++i) list.push_back(i);
      for (int i=1010; i<=1015; ++i) list.push_back(i+7);
      for (int i=1016; i<=1039; ++i) list.push_back(i+14);
      for (int i=1040; i<=1050; ++i) list.push_back(i+22);
      for (int i=1051; i<=1056; ++i) list.push_back(i+27);
      for (int i=1057; i<=1065; ++i) list.push_back(i+52);
      addVersionMap(2, 1000, list);

      list.clear();
      for (int i=1000; i<=1029; ++i) list.push_back(i);
      for (int i=1030; i<=1123; ++i) list.push_back(i+17);
      addVersionMap(3, 1000, list);

      list.clear();
      for (int i=1000; i<=1126; ++i) list.push_back(i);
      for (int i=1127; i<=1140; ++i) list.push_back(i+45);
      addVersionMap(4, 1000, list);

      static int const(what[])= {
        StarAttribute::XATTR_LINESTYLE, StarAttribute::XATTR_LINEDASH, StarAttribute::XATTR_LINEWIDTH, StarAttribute::XATTR_LINECOLOR,
        StarAttribute::XATTR_LINESTART, StarAttribute::XATTR_LINEEND, StarAttribute::XATTR_LINESTARTWIDTH, StarAttribute::XATTR_LINEENDWIDTH,
        StarAttribute::XATTR_LINESTARTCENTER, StarAttribute::XATTR_LINEENDCENTER, StarAttribute::XATTR_LINETRANSPARENCE, StarAttribute::XATTR_LINEJOINT,
        StarAttribute::XATTR_LINERESERVED2, StarAttribute::XATTR_LINERESERVED3, StarAttribute::XATTR_LINERESERVED4,StarAttribute::XATTR_LINERESERVED5,
        StarAttribute::XATTR_LINERESERVED_LAST, StarAttribute::XATTR_SET_LINE, StarAttribute::XATTR_FILLSTYLE, StarAttribute::XATTR_FILLCOLOR,

        StarAttribute::XATTR_FILLGRADIENT, StarAttribute::XATTR_FILLHATCH, StarAttribute::XATTR_FILLBITMAP, StarAttribute::XATTR_FILLTRANSPARENCE,
        StarAttribute::XATTR_GRADIENTSTEPCOUNT, StarAttribute::XATTR_FILLBMP_TILE, StarAttribute::XATTR_FILLBMP_POS, StarAttribute::XATTR_FILLBMP_SIZEX,
        StarAttribute::XATTR_FILLBMP_SIZEY, StarAttribute::XATTR_FILLFLOATTRANSPARENCE, StarAttribute::XATTR_FILLRESERVED2, StarAttribute::XATTR_FILLBMP_SIZELOG,
        StarAttribute::XATTR_FILLBMP_TILEOFFSETX, StarAttribute::XATTR_FILLBMP_TILEOFFSETY, StarAttribute::XATTR_FILLBMP_STRETCH, StarAttribute::XATTR_FILLRESERVED3,
        StarAttribute::XATTR_FILLRESERVED4, StarAttribute::XATTR_FILLRESERVED5, StarAttribute::XATTR_FILLRESERVED6, StarAttribute::XATTR_FILLRESERVED7,

        StarAttribute::XATTR_FILLRESERVED8, StarAttribute::XATTR_FILLBMP_POSOFFSETX, StarAttribute::XATTR_FILLBMP_POSOFFSETY, StarAttribute::XATTR_FILLBACKGROUND,
        StarAttribute::XATTR_FILLRESERVED10, StarAttribute::XATTR_FILLRESERVED11, StarAttribute::XATTR_FILLRESERVED_LAST, StarAttribute::XATTR_SET_FILL,
        StarAttribute::XATTR_FORMTXTSTYLE, StarAttribute::XATTR_FORMTXTADJUST, StarAttribute::XATTR_FORMTXTDISTANCE, StarAttribute::XATTR_FORMTXTSTART,
        StarAttribute::XATTR_FORMTXTMIRROR, StarAttribute::XATTR_FORMTXTOUTLINE, StarAttribute::XATTR_FORMTXTSHADOW, StarAttribute::XATTR_FORMTXTSHDWCOLOR,
        StarAttribute::XATTR_FORMTXTSHDWXVAL, StarAttribute::XATTR_FORMTXTSHDWYVAL, StarAttribute::XATTR_FORMTXTSTDFORM, StarAttribute::XATTR_FORMTXTHIDEFORM,

        StarAttribute::XATTR_FORMTXTSHDWTRANSP, StarAttribute::XATTR_FTRESERVED2, StarAttribute::XATTR_FTRESERVED3, StarAttribute::XATTR_FTRESERVED4,
        StarAttribute::XATTR_FTRESERVED5, StarAttribute::XATTR_FTRESERVED_LAST, StarAttribute::XATTR_SET_TEXT,

        // SDR 1067...
        StarAttribute::SDRATTR_SHADOW, StarAttribute::SDRATTR_SHADOWCOLOR, StarAttribute::SDRATTR_SHADOWXDIST, StarAttribute::SDRATTR_SHADOWYDIST,
        StarAttribute::SDRATTR_SHADOWTRANSPARENCE, StarAttribute::SDRATTR_SHADOW3D, StarAttribute::SDRATTR_SHADOWPERSP, StarAttribute::SDRATTR_SHADOWRESERVE1,
        StarAttribute::SDRATTR_SHADOWRESERVE2, StarAttribute::SDRATTR_SHADOWRESERVE3, StarAttribute::SDRATTR_SHADOWRESERVE4, StarAttribute::SDRATTR_SHADOWRESERVE5,
        StarAttribute::SDRATTR_SET_SHADOW, StarAttribute::SDRATTR_CAPTIONTYPE, StarAttribute::SDRATTR_CAPTIONFIXEDANGLE, StarAttribute::SDRATTR_CAPTIONANGLE,
        StarAttribute::SDRATTR_CAPTIONGAP, StarAttribute::SDRATTR_CAPTIONESCDIR, StarAttribute::SDRATTR_CAPTIONESCISREL, StarAttribute::SDRATTR_CAPTIONESCREL,

        StarAttribute::SDRATTR_CAPTIONESCABS, StarAttribute::SDRATTR_CAPTIONLINELEN, StarAttribute::SDRATTR_CAPTIONFITLINELEN, StarAttribute::SDRATTR_CAPTIONRESERVE1,
        StarAttribute::SDRATTR_CAPTIONRESERVE2, StarAttribute::SDRATTR_CAPTIONRESERVE3, StarAttribute::SDRATTR_CAPTIONRESERVE4, StarAttribute::SDRATTR_CAPTIONRESERVE5,
        StarAttribute::SDRATTR_SET_CAPTION, StarAttribute::SDRATTR_SET_OUTLINER, StarAttribute::SDRATTR_ECKENRADIUS, StarAttribute::SDRATTR_TEXT_MINFRAMEHEIGHT,
        StarAttribute::SDRATTR_TEXT_AUTOGROWHEIGHT, StarAttribute::SDRATTR_TEXT_FITTOSIZE, StarAttribute::SDRATTR_TEXT_LEFTDIST, StarAttribute::SDRATTR_TEXT_RIGHTDIST,
        StarAttribute::SDRATTR_TEXT_UPPERDIST, StarAttribute::SDRATTR_TEXT_LOWERDIST, StarAttribute::SDRATTR_TEXT_VERTADJUST, StarAttribute::SDRATTR_TEXT_MAXFRAMEHEIGHT,

        StarAttribute::SDRATTR_TEXT_MINFRAMEWIDTH, StarAttribute::SDRATTR_TEXT_MAXFRAMEWIDTH, StarAttribute::SDRATTR_TEXT_AUTOGROWWIDTH, StarAttribute::SDRATTR_TEXT_HORZADJUST,
        StarAttribute::SDRATTR_TEXT_ANIKIND, StarAttribute::SDRATTR_TEXT_ANIDIRECTION, StarAttribute::SDRATTR_TEXT_ANISTARTINSIDE, StarAttribute::SDRATTR_TEXT_ANISTOPINSIDE,
        StarAttribute::SDRATTR_TEXT_ANICOUNT, StarAttribute::SDRATTR_TEXT_ANIDELAY, StarAttribute::SDRATTR_TEXT_ANIAMOUNT, StarAttribute::SDRATTR_TEXT_CONTOURFRAME,
        StarAttribute::SDRATTR_AUTOSHAPE_ADJUSTMENT, StarAttribute::SDRATTR_XMLATTRIBUTES, StarAttribute::SDRATTR_RESERVE15, StarAttribute::SDRATTR_RESERVE16,
        StarAttribute::SDRATTR_RESERVE17, StarAttribute::SDRATTR_RESERVE18, StarAttribute::SDRATTR_RESERVE19, StarAttribute::SDRATTR_SET_MISC,

        StarAttribute::SDRATTR_EDGEKIND, StarAttribute::SDRATTR_EDGENODE1HORZDIST, StarAttribute::SDRATTR_EDGENODE1VERTDIST, StarAttribute::SDRATTR_EDGENODE2HORZDIST,
        StarAttribute::SDRATTR_EDGENODE2VERTDIST, StarAttribute::SDRATTR_EDGENODE1GLUEDIST, StarAttribute::SDRATTR_EDGENODE2GLUEDIST, StarAttribute::SDRATTR_EDGELINEDELTAANZ,
        StarAttribute::SDRATTR_EDGELINE1DELTA, StarAttribute::SDRATTR_EDGELINE2DELTA, StarAttribute::SDRATTR_EDGELINE3DELTA, StarAttribute::SDRATTR_EDGERESERVE02,
        StarAttribute::SDRATTR_EDGERESERVE03, StarAttribute::SDRATTR_EDGERESERVE04, StarAttribute::SDRATTR_EDGERESERVE05, StarAttribute::SDRATTR_EDGERESERVE06,
        StarAttribute::SDRATTR_EDGERESERVE07, StarAttribute::SDRATTR_EDGERESERVE08, StarAttribute::SDRATTR_EDGERESERVE09, StarAttribute::SDRATTR_SET_EDGE,

        StarAttribute::SDRATTR_MEASUREKIND, StarAttribute::SDRATTR_MEASURETEXTHPOS, StarAttribute::SDRATTR_MEASURETEXTVPOS, StarAttribute::SDRATTR_MEASURELINEDIST,
        StarAttribute::SDRATTR_MEASUREHELPLINEOVERHANG, StarAttribute::SDRATTR_MEASUREHELPLINEDIST, StarAttribute::SDRATTR_MEASUREHELPLINE1LEN, StarAttribute::SDRATTR_MEASUREHELPLINE2LEN,
        StarAttribute::SDRATTR_MEASUREBELOWREFEDGE, StarAttribute::SDRATTR_MEASURETEXTROTA90, StarAttribute::SDRATTR_MEASURETEXTUPSIDEDOWN, StarAttribute::SDRATTR_MEASUREOVERHANG,
        StarAttribute::SDRATTR_MEASUREUNIT, StarAttribute::SDRATTR_MEASURESCALE, StarAttribute::SDRATTR_MEASURESHOWUNIT, StarAttribute::SDRATTR_MEASUREFORMATSTRING,
        StarAttribute::SDRATTR_MEASURETEXTAUTOANGLE, StarAttribute::SDRATTR_MEASURETEXTAUTOANGLEVIEW, StarAttribute::SDRATTR_MEASURETEXTISFIXEDANGLE, StarAttribute::SDRATTR_MEASURETEXTFIXEDANGLE,
        // 1167
        StarAttribute::SDRATTR_MEASUREDECIMALPLACES, StarAttribute::SDRATTR_MEASURERESERVE05, StarAttribute::SDRATTR_MEASURERESERVE06, StarAttribute::SDRATTR_MEASURERESERVE07,
        StarAttribute::SDRATTR_SET_MEASURE, StarAttribute::SDRATTR_CIRCKIND, StarAttribute::SDRATTR_CIRCSTARTANGLE, StarAttribute::SDRATTR_CIRCENDANGLE,
        StarAttribute::SDRATTR_CIRCRESERVE0, StarAttribute::SDRATTR_CIRCRESERVE1, StarAttribute::SDRATTR_CIRCRESERVE2, StarAttribute::SDRATTR_CIRCRESERVE3,
        StarAttribute::SDRATTR_SET_CIRC, StarAttribute::SDRATTR_OBJMOVEPROTECT, StarAttribute::SDRATTR_OBJSIZEPROTECT, StarAttribute::SDRATTR_OBJPRINTABLE,
        StarAttribute::SDRATTR_LAYERID, StarAttribute::SDRATTR_LAYERNAME, StarAttribute::SDRATTR_OBJECTNAME, StarAttribute::SDRATTR_ALLPOSITIONX,

        StarAttribute::SDRATTR_ALLPOSITIONY, StarAttribute::SDRATTR_ALLSIZEWIDTH, StarAttribute::SDRATTR_ALLSIZEHEIGHT, StarAttribute::SDRATTR_ONEPOSITIONX,
        StarAttribute::SDRATTR_ONEPOSITIONY, StarAttribute::SDRATTR_ONESIZEWIDTH, StarAttribute::SDRATTR_ONESIZEHEIGHT, StarAttribute::SDRATTR_LOGICSIZEWIDTH,
        StarAttribute::SDRATTR_LOGICSIZEHEIGHT, StarAttribute::SDRATTR_ROTATEANGLE, StarAttribute::SDRATTR_SHEARANGLE, StarAttribute::SDRATTR_MOVEX,
        StarAttribute::SDRATTR_MOVEY, StarAttribute::SDRATTR_RESIZEXONE, StarAttribute::SDRATTR_RESIZEYONE, StarAttribute::SDRATTR_ROTATEONE,
        StarAttribute::SDRATTR_HORZSHEARONE, StarAttribute::SDRATTR_VERTSHEARONE, StarAttribute::SDRATTR_RESIZEXALL, StarAttribute::SDRATTR_RESIZEYALL,

        StarAttribute::SDRATTR_ROTATEALL, StarAttribute::SDRATTR_HORZSHEARALL, StarAttribute::SDRATTR_VERTSHEARALL, StarAttribute::SDRATTR_TRANSFORMREF1X,
        StarAttribute::SDRATTR_TRANSFORMREF1Y, StarAttribute::SDRATTR_TRANSFORMREF2X, StarAttribute::SDRATTR_TRANSFORMREF2Y, StarAttribute::SDRATTR_TEXTDIRECTION,
        StarAttribute::SDRATTR_NOTPERSISTRESERVE2, StarAttribute::SDRATTR_NOTPERSISTRESERVE3, StarAttribute::SDRATTR_NOTPERSISTRESERVE4, StarAttribute::SDRATTR_NOTPERSISTRESERVE5,
        StarAttribute::SDRATTR_NOTPERSISTRESERVE6, StarAttribute::SDRATTR_NOTPERSISTRESERVE7, StarAttribute::SDRATTR_NOTPERSISTRESERVE8, StarAttribute::SDRATTR_NOTPERSISTRESERVE9,
        StarAttribute::SDRATTR_NOTPERSISTRESERVE10, StarAttribute::SDRATTR_NOTPERSISTRESERVE11, StarAttribute::SDRATTR_NOTPERSISTRESERVE12, StarAttribute::SDRATTR_NOTPERSISTRESERVE13,

        StarAttribute::SDRATTR_NOTPERSISTRESERVE14, StarAttribute::SDRATTR_NOTPERSISTRESERVE15,
      };
      for (int i=0; i<int(sizeof(what)/sizeof(int)); ++i)
        m_idToAttributeList.push_back(what[i]);

#if 0


// then from 1229=SDRATTR_GRAFRED sdr svddef.hxx and svx_svdattr.cxx
#endif
    }
  }
  //! returns true if the value is in expected range
  int isInRange(int which) const
  {
    return which>=m_verStart&&which<=m_verEnd;
  }
  //! add a new version map
  void addVersionMap(uint16_t nVers, uint16_t nStart, std::vector<int> const &list)
  {
    // SfxItemPool::SetVersionMap
    if (nVers<=m_currentVersion)
      return;
    m_versionList.push_back(Version(int(nVers), int(nStart), list));
    m_currentVersion=nVers;
    Version const &vers=m_versionList.back();
    if (vers.m_invertListMap.empty()) return;
    int min=vers.m_invertListMap.begin()->first;
    if (m_verStart>min) m_verStart=min;
    int max=(--vers.m_invertListMap.end())->first;
    if (m_verEnd<max) m_verEnd=max;
  }
  //! try to return ???
  int getWhich(int nFileWhich) const
  {
    // polio.cxx: SfxItemPool::GetNewWhich
    if (nFileWhich<m_verStart||nFileWhich>m_verEnd) {
      // to do implement recursif method
      STOFF_DEBUG_MSG(("StarItemPoolInternal::State::GetWhich: recursive method is not implemented\n"));
      return 0;
    }
    if (m_loadingVersion>m_currentVersion) {
      for (size_t i=m_versionList.size(); i>0;) {
        Version const &vers=m_versionList[--i];
        if (vers.m_version<=m_currentVersion)
          break;
        if (vers.m_invertListMap.find(nFileWhich)==vers.m_invertListMap.end())
          return 0;
        nFileWhich=vers.m_start+vers.m_invertListMap.find(nFileWhich)->second;
      }
    }
    else if (m_loadingVersion<m_currentVersion) {
      for (size_t i=0; i<m_versionList.size(); ++i) {
        Version const &vers=m_versionList[i];
        if (vers.m_version<=m_loadingVersion)
          continue;
        if (nFileWhich<vers.m_start || nFileWhich>=vers.m_start+(int) vers.m_list.size()) {
          STOFF_DEBUG_MSG(("StarItemPoolInternal::State::GetWhich: argh nFileWhich is not in good range\n"));
          break;
        }
        else
          nFileWhich=vers.m_list[size_t(nFileWhich-vers.m_start)];
      }
    }
    return nFileWhich;
  }
  //! the document
  StarDocument &m_document;
  //! the majorVersion
  int m_majorVersion;
  //! the minorVersion
  int m_minorVersion;
  //! the loading version
  int m_loadingVersion;
  //! the name
  librevenge::RVNGString m_name;
  //! the current version
  int m_currentVersion;
  //! the minimum version
  int m_verStart;
  //! the maximum version
  int m_verEnd;
  //! the list of version
  std::vector<Version> m_versionList;
  //! list whichId to attribute list
  std::vector<int> m_idToAttributeList;
private:
  State(State const &orig);
  State operator=(State const &orig);
};

}

////////////////////////////////////////////////////////////
// constructor/destructor, ...
////////////////////////////////////////////////////////////
StarItemPool::StarItemPool(StarDocument &doc) : m_state(new StarItemPoolInternal::State(doc))
{
}

StarItemPool::~StarItemPool()
{
}

int StarItemPool::getVersion() const
{
  return m_state->m_majorVersion;
}

bool StarItemPool::readAttribute(StarZone &zone, int which, int vers, long endPos)
{
  if (which<m_state->m_verStart || which>=m_state->m_verStart+int(m_state->m_idToAttributeList.size()) ||
      !m_state->m_document.getSDWParser()) {
    STOFFInputStreamPtr input=zone.input();
    long pos=input->tell();
    libstoff::DebugFile &ascii=zone.ascii();
    libstoff::DebugStream f;
    f << "Entries(StarAttribute)["<< zone.getRecordLevel() << "]:wh=" << which;
    if (!m_state->m_idToAttributeList.empty())
      f << "##";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(endPos, librevenge::RVNG_SEEK_SET);
    return true;
  }
  StarAttribute attribute;
  zone.openDummyRecord();
  bool ok=attribute.readAttribute(zone, m_state->m_idToAttributeList[size_t(which-m_state->m_verStart)],
                                  vers, endPos, m_state->m_document);
  zone.closeDummyRecord();
  return ok;
}

bool StarItemPool::read(StarZone &zone)
{
  // reinit all
  m_state.reset(new StarItemPoolInternal::State(m_state->m_document));

  STOFFInputStreamPtr input=zone.input();
  libstoff::DebugFile &ascii=zone.ascii();
  long endPos=zone.getRecordLevel()>0 ?  zone.getRecordLastPosition() : input->size();
  libstoff::DebugStream f;
  f << "Entries(PoolDef)["<< zone.getRecordLevel() << "]:";
  long pos=input->tell();
  if (pos+18>endPos) {
    STOFF_DEBUG_MSG(("StarItemPool::read: the zone seems too short\n"));
    return false;
  }
  uint16_t tag;
  // poolio.cxx SfxItemPool::Load
  uint8_t nMajorVers, nMinorVers;
  *input >> tag >> nMajorVers >> nMinorVers;
  if (tag==0x1111)
    f << "v4,";
  else if (tag==0xbbbb)
    f << "v5,";
  else {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  if (nMajorVers==1) {
    m_state->m_majorVersion=1;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return readV1(zone);
  }
  if (nMajorVers!=2) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  *input>>tag;
  if (tag!=0xffff) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  m_state->m_majorVersion=2;
  m_state->m_minorVersion=int(nMinorVers);
  f << "version=2,";
  if (m_state->m_minorVersion) f << "vers[minor]=" << m_state->m_minorVersion << ",";
  input->seek(4, librevenge::RVNG_SEEK_CUR); // 0,0

  char type; // always 1
  if (input->peek()!=1 || !zone.openSfxRecord(type)) {
    STOFF_DEBUG_MSG(("StarItemPool::read: can not open the sfx record\n"));
    m_state->m_majorVersion=0;
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  endPos=zone.getRecordLastPosition();
  if (input->tell()==endPos) {
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    zone.closeSfxRecord(type, "PoolDef");
    return true;
  }
  // string part
  char type1;
  if (input->peek()!=16 || !zone.openSfxRecord(type1)) {
    STOFF_DEBUG_MSG(("StarItemPool::read: can not open the string sfx record\n"));
    f << "###openString";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    zone.closeSfxRecord(type, "PoolDef");
    return true;
  }
  int16_t val;
  *input >> val;
  if (val) {
    f << "loadingVersion=" << val << ",";
    m_state->m_loadingVersion=val;
  }
  librevenge::RVNGString string;
  if (!zone.readString(string)) {
    STOFF_DEBUG_MSG(("StarItemPool::read: can not read the name\n"));
    f << "###name";
  }
  if (!string.empty()) {
    m_state->setPoolName(string);
    f << "name[ext]=" << string.cstr() << ",";
  }
  zone.closeSfxRecord(type1, "PoolDef");

  ascii.addPos(pos);
  ascii.addNote(f.str().c_str());

  pos=input->tell();
  f.str("");
  f << "PoolDef[versMap]:";
  StarItemPoolInternal::SfxMultiRecord mRecord;
  if (!mRecord.open(zone) || mRecord.getHeaderTag()!=0x20) {
    STOFF_DEBUG_MSG(("StarItemPool::read: can not open the versionMap sfx record\n"));
    f << "###openVersionMap";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    if (mRecord.isOpened()) mRecord.close("PoolDef");
    zone.closeSfxRecord(type, "PoolDef");
    return true;
  }
  f << mRecord;
  ascii.addPos(pos);
  ascii.addNote(f.str().c_str());
  int n=0;
  while (mRecord.getNewContent("PoolDef")) {
    pos=input->tell();
    f.str("");
    f << "PoolDef[versMap-" << n++ << "]:";
    uint16_t nVers, nStart, nEnd;
    *input >> nVers >> nStart >> nEnd;
    f << "vers=" << nVers << "," << nStart << "<->" << nEnd << ",";
    if (nStart>nEnd || input->tell()+2*(nEnd-nStart+1) > mRecord.getLastContentPosition()) {
      STOFF_DEBUG_MSG(("StarItemPool::read: can not find start|end pos\n"));
      f << "###badStartEnd";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }
    f << "pos=[";
    std::vector<int> listData;
    for (uint16_t i=nStart; i<=nEnd; ++i) {
      uint16_t nPos;
      *input>>nPos;
      listData.push_back(int(nPos));
      f << nPos << ",";
    }
    f << "],";
    if (!listData.empty()) m_state->addVersionMap(nVers,nStart,listData);

    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
  }
  mRecord.close("PoolDef");

  for (int step=0; step<2; ++step) {
    std::string wh(step==0 ? "attrib" : "default");
    pos=input->tell();
    f.str("");
    f << "PoolDef[" << wh << "]:";
    if (!mRecord.open(zone)) {
      STOFF_DEBUG_MSG(("StarItemPool::read: can not open the sfx record\n"));
      f << "###" << wh;
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      zone.closeSfxRecord(type, "PoolDef");
      return true;
    }
    f << mRecord;

    if (mRecord.getHeaderTag()!=(step==0 ? 0x30:0x50)) {
      STOFF_DEBUG_MSG(("StarItemPool::read: can not find the pool which tag\n"));
      f << "###tag";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      mRecord.close("PoolDef");
      continue;
    }
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());

    n=0;
    while (mRecord.getNewContent("PoolDef")) {
      pos=input->tell();
      f.str("");
      f << "PoolDef[" << wh << "-" << n++ << "]:";
      uint16_t nWhich, nVersion, nCount=1;
      *input >> nWhich >> nVersion;
      if (step==0) *input >> nCount;
      int which=nWhich;
      if (m_state->m_currentVersion!=m_state->m_loadingVersion) which=m_state->getWhich(which);
      if (!m_state->isInRange(which)) {
        STOFF_DEBUG_MSG(("StarItemPool::read: the which value seems bad\n"));
        f << "###";
      }
      f << "wh=" << which-m_state->m_verStart << ", vers=" << nVersion << ", count=" << nCount << ",";
      static bool first=true;
      if (first) {
        STOFF_DEBUG_MSG(("StarItemPool::read: reading attribute is not implemented\n"));
        first=false;
      }
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      pos=input->tell();
      if (step==0) {
        StarItemPoolInternal::SfxMultiRecord mRecord1;
        f.str("");
        f << "Entries(StarAttribute):inPool,";
        if (!mRecord1.open(zone)) {
          STOFF_DEBUG_MSG(("StarItemPool::read: can not open record1\n"));
          f << "###record1";
          ascii.addPos(pos);
          ascii.addNote(f.str().c_str());
        }
        else {
          f << mRecord1;
          ascii.addPos(pos);
          ascii.addNote(f.str().c_str());
          while (mRecord1.getNewContent("StarAttribute")) {
            pos=input->tell();
            f.str("");
            f << "StarAttribute:inPool,wh=" <<  which-m_state->m_verStart << ",";
            uint16_t nRef;
            *input>>nRef;
            f << "ref=" << nRef << ",";
            if (!readAttribute(zone, which, (int) nVersion, mRecord1.getLastContentPosition()))
              f << "###";
            else if (input->tell()!=mRecord1.getLastContentPosition()) {
              STOFF_DEBUG_MSG(("StarItemPool::read: find extra attrib data\n"));
              f << "###extra";
            }
            input->seek(mRecord1.getLastContentPosition(), librevenge::RVNG_SEEK_SET);
            ascii.addPos(pos);
            ascii.addNote(f.str().c_str());
          }
          mRecord1.close("StarAttribute");
        }
      }
      else {
        if (!readAttribute(zone, which, (int) nVersion, mRecord.getLastContentPosition())) {
          f.str("");
          f << "Entries(StarAttribute)[" <<  which-m_state->m_verStart << "]:";
          ascii.addPos(pos);
          ascii.addNote(f.str().c_str());
        }
        else if (input->tell()!=mRecord.getLastContentPosition()) {
          STOFF_DEBUG_MSG(("StarItemPool::read: find extra attrib data\n"));
          ascii.addPos(pos);
          ascii.addNote("extra###");
        }
      }
      input->seek(mRecord.getLastContentPosition(), librevenge::RVNG_SEEK_SET);
    }
    mRecord.close("PoolDef");
  }

  zone.closeSfxRecord(type, "PoolDef");
  return true;
}

bool StarItemPool::readV1(StarZone &zone)
{
  STOFFInputStreamPtr input=zone.input();
  libstoff::DebugFile &ascii=zone.ascii();
  long endPos=zone.getRecordLevel()>0 ?  zone.getRecordLastPosition() : input->size();
  libstoff::DebugStream f;
  f << "Entries(PoolDef)[" << zone.getRecordLevel() << "]:";
  long pos=input->tell();
  if (pos+18>endPos) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: the zone seems too short\n"));
    return false;
  }
  uint16_t tag;
  // poolio.cxx SfxItemPool::Load1_Impl
  uint8_t nMajorVers, nMinorVers;
  *input >> tag >> nMajorVers >> nMinorVers;
  if (tag==0x1111)
    f << "v4,";
  else if (tag==0xbbbb)
    f << "v5,";
  else {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  if (nMajorVers!=1) {
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  f << "version=" << int(nMajorVers) << ",vers[minor]=" << int(nMinorVers) << ",";
  if (nMinorVers>=3) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: find a minor version >= 3\n"));
    f << "###";
  }

  // SfxItemPool::Load1_Impl
  if (nMinorVers>=2) {
    int16_t nLoadingVersion;
    *input >> nLoadingVersion;
    m_state->m_loadingVersion=int(nLoadingVersion);
    if (nLoadingVersion) f << "vers[loading]=" << nLoadingVersion << ",";
  }
  librevenge::RVNGString string;
  if (!zone.readString(string)) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: can not read the name\n"));
    f << "###name";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  if (!string.empty()) {
    m_state->setPoolName(string);
    f << "name[ext]=" << string.cstr() << ",";
  }
  uint32_t attribSize;
  *input>>attribSize;
  long attribPos=input->tell();
  if (attribPos+long(attribSize)+10>endPos) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: attribSize is bad\n"));
    f << "###";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  else if (attribSize) {
    f << "attr[sz]=" << attribSize << ",";
    input->seek((long)attribSize, librevenge::RVNG_SEEK_CUR);
  }
  else {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: attribSize\n"));
    f << "###attrib[sz]==0,";
  }

  *input >> tag;
  if (tag!=0x3333) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find tag size\n"));
    f << "###tag=" << std::hex << tag << std::dec;
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  uint32_t tableSize;
  *input>>tableSize;
  long tablePos=input->tell();
  long beginEndPos=tablePos+(long) tableSize;
  if (beginEndPos+4>endPos) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: tableSize is bad\n"));
    f << "###";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }
  else if (tableSize)
    f << "table[sz]=" << tableSize << ",";
  else if (nMinorVers>=3) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: tableSize is null for version 3\n"));
    f << "###table[sz]==0,";
  }
  ascii.addPos(pos);
  ascii.addNote(f.str().c_str());

  long endTablePos=tablePos+(long)tableSize;
  if (nMinorVers>=3 && tableSize>=4) { // CHECKME: never seens
    input->seek(endTablePos-4, librevenge::RVNG_SEEK_SET);
    pos=(long) input->readULong(4);
    endTablePos-=4;
    if (pos<tablePos || pos>=endTablePos) {
      STOFF_DEBUG_MSG(("StarItemPool::readV1: arrgh can not find versionmap position\n"));
    }
    else {
      long lastVersMapPos=endTablePos;
      endTablePos=pos;
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      f.str("");
      f << "PoolDef[versionMap]:";
      uint16_t nVerCount;
      *input >> nVerCount;
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      for (int i=0; i<int(nVerCount); ++i) {
        pos=input->tell();
        f.str("");
        f << "PoolDef[versionMap" << i << "]:";
        uint16_t nVers, nStart, nEnd;
        *input >> nVers >> nStart >> nEnd;
        f << "vers=" << nVers << "," << nStart << "-" << nEnd << ",";
        int N=int(nEnd)+1-int(nStart);
        if (N<0 || pos+6+2*N>lastVersMapPos) {
          STOFF_DEBUG_MSG(("StarItemPool::readV1: arrgh nBytes is bad\n"));
          f << "###";
          ascii.addPos(pos);
          ascii.addNote(f.str().c_str());
          break;
        }
        f << "val=[";
        for (int j=0; j<N; ++j) {
          uint16_t val;
          *input >> val;
          if (val) f << val << ",";
          else
            f << "_,";
        }
        f << "],";
        ascii.addPos(pos);
        ascii.addNote(f.str().c_str());
      }
    }
  }

  // now read the table
  input->seek(tablePos, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "PoolDef[table]:";
  std::vector<uint32_t> sizeAttr;
  f << "size=[";
  while (input->tell()+4<=endTablePos) {
    uint32_t sz;
    *input>>sz;
    sizeAttr.push_back(sz);
    f << sz << ",";
  }
  f << "],";
  ascii.addPos(tablePos-6);
  ascii.addNote(f.str().c_str());

  // now read the attribute
  input->seek(attribPos, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  long endDataPos=attribPos+(long) attribSize;
  f.str("");
  f << "PoolDef[attrib]:";
  bool ok=false;
  if (attribSize!=0)  {
    *input >> tag;
    if (tag!=0x2222) {
      STOFF_DEBUG_MSG(("StarItemPool::readV1: tag is bad \n"));
      f << "###tag=" << std::hex << tag << std::dec;
    }
    else
      ok=true;
  }
  ascii.addPos(attribPos-4);
  ascii.addNote(f.str().c_str());
  size_t n=0;
  while (ok) {
    pos=input->tell();
    f.str("");
    f << "PoolDef[attrib" << n << "]:";
    if (pos+2 > endDataPos) {
      ok=false;

      STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find last attrib\n"));
      f << "###noLast,";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }
    uint16_t nWhich;
    *input >> nWhich;
    if (nWhich==0) {
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }

    static bool first=true;
    if (first) {
      STOFF_DEBUG_MSG(("StarItemPool::readV1: reading attribute is not implemented\n"));
      first=false;
    }
    uint16_t nSlot, nVersion, nCount;
    *input >> nSlot >> nVersion >> nCount;
    int which=nWhich;
    if (m_state->m_currentVersion!=m_state->m_loadingVersion) which=m_state->getWhich(which);
    if (!m_state->isInRange(which)) {
      STOFF_DEBUG_MSG(("StarItemPool::readV1: the which value seems bad\n"));
      f << "###";
    }
    f << "wh=" << which << "[" << std::hex << nSlot << std::dec << "], vers=" << nVersion << ", count=" << nCount << ",";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());

    for (int i=0; i<nCount; ++i) {
      pos=input->tell();

      f.str("");
      f << "StarAttribute:inPool,wh=" <<  which-m_state->m_verStart << ",";
      if (n >= sizeAttr.size() || pos+(long)sizeAttr[n]>endDataPos) {
        ok=false;

        STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find attrib size\n"));
        f << "###badSize,";
        ascii.addPos(pos);
        ascii.addNote(f.str().c_str());
        break;
      }
      if (!sizeAttr[n]) continue;

      uint16_t nRef;
      *input>>nRef;
      f << "ref=" << nRef << ",";
      if (nRef) {
        if (!readAttribute(zone, which, (int) nVersion, pos+(long) sizeAttr[n]))
          f << "###";
      }
      if (input->tell()!=pos+(long)sizeAttr[n]) {
        STOFF_DEBUG_MSG(("StarItemPool::readV1: find extra attrib data\n"));
        f << "###extra,";
        if (input->tell()!=pos+2)
          ascii.addDelimiter(input->tell(),'|');
        input->seek(pos+(long) sizeAttr[n], librevenge::RVNG_SEEK_SET);
      }
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      ++n;
    }
    if (!ok) break;
  }
  if (ok) {
    pos=input->tell();
    f.str("");
    f << "PoolDef[default]:";
    if (nMinorVers>0) {
      *input>>tag;
      if (tag!=0x4444) {
        STOFF_DEBUG_MSG(("StarItemPool::readV1: default tag is bad \n"));
        f << "###tag=" << std::hex << tag << std::dec;
        ascii.addPos(pos);
        ascii.addNote(f.str().c_str());
        ok=false;
      }
    }
  }
  if (ok) {
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
  }

  while (ok) {
    pos=input->tell();
    f.str("");
    f << "PoolDef[default" << n << "]:";
    if (pos+2 > endDataPos) {
      ok=false;

      STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find last attrib\n"));
      f << "###noLast,";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }
    uint16_t nWhich;
    *input >> nWhich;
    if (nWhich==0) {
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }
    uint16_t nSlot, nVersion;
    *input >> nSlot >> nVersion;
    f << "wh=" << nWhich << "[" << std::hex << nSlot << std::dec << "], vers=" << nVersion << ",";
    if (n >= sizeAttr.size() || sizeAttr[n]<6 || pos+(long) sizeAttr[n]>endDataPos) {
      ok=false;

      STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find attrib size\n"));
      f << "###badSize,";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      break;
    }
    if (sizeAttr[n]>6) ascii.addDelimiter(input->tell(),'|');
    input->seek(pos+(long) sizeAttr[n++], librevenge::RVNG_SEEK_SET);
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
  }

  input->seek(beginEndPos, librevenge::RVNG_SEEK_SET);
  pos=input->tell();
  f.str("");
  f << "PoolDef[end]:";
  uint16_t tag1;
  *input >> tag >> tag1;
  if (tag!=0xeeee || tag1!=0xeeee) {
    STOFF_DEBUG_MSG(("StarItemPool::readV1: can not find end tag\n"));
    f << "###tag=" << std::hex << tag << std::dec;
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
    input->seek(pos, librevenge::RVNG_SEEK_SET);
    return false;
  }

  ascii.addPos(pos);
  ascii.addNote(f.str().c_str());
  return true;
}

bool StarItemPool::readStyle(StarZone &zone, int poolVersion)
{
  STOFFInputStreamPtr input=zone.input();
  libstoff::DebugFile &ascii=zone.ascii();
  long pos=input->tell();
  libstoff::DebugStream f;
  f << "Entries(SfxStylePool)[" << zone.getRecordLevel() << "]:pool[vers]=" << poolVersion << ",";
  if (poolVersion!=1 && poolVersion!=2) {
    STOFF_DEBUG_MSG(("StarItemPool::readStyle: reading version 1 is not implemented\n"));
    return false;
  }
  char type;
  uint16_t charSet, nCount;

  bool helpIdSize32=true, ok=true;
  StarItemPoolInternal::SfxMultiRecord mRecord;
  if (poolVersion==1) {
    // style.cxx SfxStyleSheetBasePool::Load1_Impl
    *input >> charSet;
    if (charSet==50) {
      f << "v50,";
      *input >> charSet;
    }
    else
      helpIdSize32=false;
    if (charSet) f << "char[set]="<< charSet << ",";
    *input >> nCount;
    f << "n=" << nCount << ",";
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
  }
  else {
    // style.cxx SfxStyleSheetBasePool::Load(vers==2)
    if (input->peek()!=3 || !zone.openSfxRecord(type)) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not open the first zone\n"));
      input->seek(pos, librevenge::RVNG_SEEK_SET);
      return false;
    }

    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << "SfxStylePool[header]:";
    char type1;
    if (!zone.openSfxRecord(type1)) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not open the header zone\n"));
      f << "###";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      zone.closeSfxRecord(type, "SfxStylePool");
      return false;
    }
    uint8_t headerType, headerVersion;
    uint16_t headerTag;
    *input >> headerType >> headerVersion >> headerTag;
    if (headerTag!=0x10) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not find header tag\n"));
      f << "###";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      zone.closeSfxRecord(type1, "SfxStylePool");
      zone.closeSfxRecord(type, "SfxStylePool");
    }
    if (headerVersion) f << "vers=" << int(headerVersion) << ",";
    if (headerType) f << "type=" << int(headerType) << ",";
    *input >> charSet;
    if (charSet) f << "char[set]="<< charSet << ",";
    zone.closeSfxRecord(type1, "SfxStylePool");
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());

    pos=input->tell();
    f.str("");
    f << "SfxStylePool[styles]:";
    if (!mRecord.open(zone)) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not open the versionMap sfx record\n"));
      f << "###openVersionMap";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      zone.closeSfxRecord(type, "PoolDef");
      return true;
    }
    f << mRecord;

    if (mRecord.getHeaderTag()!=0x20) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not find the version map tag\n"));
      f << "###tag";
      ok=false;
    }
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());

    nCount=mRecord.getNumRecords();
  }
  long lastPos=zone.getRecordLevel() ? zone.getRecordLastPosition() : input->size();
  for (int i=0; ok && i<int(nCount); ++i) {
    pos=input->tell();
    if (poolVersion==2) {
      if (!mRecord.getNewContent("SfxStylePool"))
        break;
      lastPos=mRecord.getLastContentPosition();
    }
    f.str("");
    f << "SfxStylePool[data" << i << "]:";
    bool readOk=true;
    librevenge::RVNGString text;
    for (int j=0; j<3; ++j) {
      if (!zone.readString(text) || input->tell()>=lastPos) {
        STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not find a string\n"));
        f << "###string";
        readOk=false;
        break;
      }
      if (text.empty()) continue;
      static char const *(wh[])= {"name","parent","follow"};
      f << wh[j] << "=" << text.cstr() << ",";
    }
    if (!readOk) {
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      if (poolVersion==1) return true;
      continue;
    }
    uint16_t nFamily, nMask, nItemCount, nVer;
    *input >> nFamily >> nMask;
    if (nFamily) f << "family=" << nFamily << ",";
    if (nMask) f << "mask=" << nMask << ",";
    if (!zone.readString(text) || input->tell()>=lastPos) {
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: can not find helpFile\n"));
      f << "###helpFile";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      if (poolVersion==1) return true;
      continue;
    }
    if (!text.empty()) f << "help[file]=" << text.cstr() << ",";
    uint32_t nHelpId, nSize;
    if (helpIdSize32)
      *input>>nHelpId;
    else {
      uint16_t tmp;
      *input >> tmp;
      nHelpId=tmp;
    }
    if (nHelpId) f << "help[id]=" << nHelpId << ",";
    *input >> nItemCount;
    if (nItemCount) {
      f << "item[count]=" << nItemCount << ",";
      input->seek(-2, librevenge::RVNG_SEEK_CUR);
      if (!StarFileManager::readSfxItemList(zone)) {
        f << "###itemList";
        ascii.addPos(pos);
        ascii.addNote(f.str().c_str());
        if (poolVersion==1) return true;
        continue;
      }
      ascii.addDelimiter(input->tell(),'|');
    }
    *input>>nVer>>nSize;
    if (nVer) f << "version=" << nVer << ",";
    if (input->tell()+long(nSize)>lastPos || (poolVersion==2 && input->tell()+long(nSize)+4<lastPos)) {
      // be strict while readSfxItemList is not sure
      STOFF_DEBUG_MSG(("StarItemPool::readStyle: something is bad\n"));
      f << "###nSize=" << nSize << ",";
      ascii.addPos(pos);
      ascii.addNote(f.str().c_str());
      if (poolVersion==1) return true;
      continue;
    }
    if (nSize) {
      f << "#size=" << nSize << ",";
      static bool first=true;
      if (first) {
        STOFF_DEBUG_MSG(("StarItemPool::readStyle: loading a sheet is not implemented\n"));
        first=false;
      }
      // normally SfxStyleSheetBase::Load but no code
      libstoff::DebugStream f2;
      f2 << "Entries(SfxBaseSheet):sz=" << nSize << ",##";
      ascii.addPos(input->tell()-4);
      ascii.addNote(f2.str().c_str());
      ascii.addDelimiter(input->tell(),'|');
      input->seek((long) nSize, librevenge::RVNG_SEEK_CUR);
    }
    ascii.addPos(pos);
    ascii.addNote(f.str().c_str());
  }

  if (poolVersion==2) {
    mRecord.close("SfxStylePool");
    zone.closeSfxRecord(type, "SfxStylePool");
  }
  return true;
}

// vim: set filetype=cpp tabstop=2 shiftwidth=2 cindent autoindent smartindent noexpandtab:
