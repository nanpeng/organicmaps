#include "indexer/feature_visibility.hpp"

#include "indexer/classificator.hpp"
#include "indexer/drawing_rules.hpp"
#include "indexer/feature.hpp"
#include "indexer/feature_data.hpp"
#include "indexer/scales.hpp"

#include "base/assert.hpp"
#include "base/checked_cast.hpp"

#include <algorithm>
#include <array>

namespace feature
{
using namespace std;

namespace
{
int CorrectScale(int scale)
{
  CHECK_LESS_OR_EQUAL(scale, scales::GetUpperStyleScale(), ());
  return min(scale, scales::GetUpperStyleScale());
}
} // namespace

void GetDrawRule(TypesHolder const & types, int level, drule::KeysT & keys)
{
  ASSERT ( keys.empty(), () );
  Classificator const & c = classif();

  auto const geomType = types.GetGeomType();
  level = CorrectScale(level);
  for (uint32_t t : types)
    c.GetObject(t)->GetSuitable(level, geomType, keys);
}

void GetDrawRule(vector<uint32_t> const & types, int level, GeomType geomType, drule::KeysT & keys)
{
  ASSERT ( keys.empty(), () );
  Classificator const & c = classif();

  level = CorrectScale(level);
  for (uint32_t t : types)
    c.GetObject(t)->GetSuitable(level, geomType, keys);
}

void FilterRulesByRuntimeSelector(FeatureType & f, int zoomLevel, drule::KeysT & keys)
{
  keys.erase_if([&f, zoomLevel](drule::Key const & key)
  {
    drule::BaseRule const * const rule = drule::rules().Find(key);
    if (rule == nullptr)
      return true;
    return !rule->TestFeature(f, zoomLevel);
  });
}

namespace
{
  class IsDrawableRulesChecker
  {
    int m_scale;
    GeomType m_gt;
    bool m_arr[3];

  public:
    IsDrawableRulesChecker(int scale, GeomType gt, int rules)
      : m_scale(scale), m_gt(gt)
    {
      m_arr[0] = rules & RULE_CAPTION;
      m_arr[1] = rules & RULE_PATH_TEXT;
      m_arr[2] = rules & RULE_SYMBOL;
    }

    bool operator() (ClassifObject const * p) const
    {
      drule::KeysT keys;
      p->GetSuitable(m_scale, m_gt, keys);

      for (auto const & k : keys)
      {
        if ((m_arr[0] && k.m_type == drule::caption) ||
            (m_arr[1] && k.m_type == drule::pathtext) ||
            (m_arr[2] && k.m_type == drule::symbol))
        {
          return true;
        }
      }

      return false;
    }
  };

  /// @name Add here classifier types that don't have drawing rules but needed for algorithms.
  /// @todo The difference between \a TypeAlwaysExists and \a IsUsefulNondrawableType is not
  /// obvious. TypeAlwaysExists are *indexed* in category search, while IsUsefulNondrawableType are *not*.
  /// The functions names and set of types looks strange now and definitely should be revised.
  /// @{

  /// Warning: Geometry of features with always existing types will be indexed in mwm on all
  /// zoom levels. If you add an always existing type to drawing types, the displacement of icons
  /// may work not correctly.
  bool TypeAlwaysExists(uint32_t type, GeomType g = GeomType::Undefined)
  {
    auto const & cl = classif();
    if (!cl.IsTypeValid(type))
      return false;

    static uint32_t const shuttle = cl.GetTypeByPath({"route", "shuttle_train"});
    static uint32_t const internet = cl.GetTypeByPath({"internet_access"});
    static uint32_t const complexEntry = cl.GetTypeByPath({"complex_entry"});

    if ((g == GeomType::Line || g == GeomType::Undefined) && type == shuttle)
      return true;

    uint8_t const typeLevel = ftype::GetLevel(type);
    ftype::TruncValue(type, 1);

    if (g != GeomType::Line)
    {
      if (type == internet)
        return true;

      // Exclude generic 1-arity types like [organic].
      if (typeLevel >= 2)
      {
        static uint32_t const arrTypes[] = {
            cl.GetTypeByPath({"organic"}),
            cl.GetTypeByPath({"recycling"})
        };
        if (base::IsExist(arrTypes, type))
          return true;
      }
    }

    return (type == complexEntry);
  }

  bool IsUsefulNondrawableType(uint32_t type, GeomType g = GeomType::Undefined)
  {
    auto const & cl = classif();
    if (!cl.IsTypeValid(type))
      return false;

    if (TypeAlwaysExists(type, g))
      return true;

    // Exclude generic 1-arity types like [wheelchair].
    if (ftype::GetLevel(type) < 2)
      return false;

    static uint32_t const hwtag = cl.GetTypeByPath({"hwtag"});
    static uint32_t const roundabout = cl.GetTypeByPath({"junction", "roundabout"});
    static uint32_t const psurface = cl.GetTypeByPath({"psurface"});

    if ((g == GeomType::Line || g == GeomType::Undefined) && type == roundabout)
      return true;

    ftype::TruncValue(type, 1);
    if (g == GeomType::Line || g == GeomType::Undefined)
    {
      if (type == hwtag || type == psurface)
        return true;
    }

    static uint32_t const arrTypes[] = {
      cl.GetTypeByPath({"wheelchair"}),
      cl.GetTypeByPath({"cuisine"})
    };
    return base::IsExist(arrTypes, type);
  }
  /// @}
}  // namespace

bool IsUsefulType(uint32_t type)
{
  return IsUsefulNondrawableType(type) || classif().GetObject(type)->IsDrawableAny();
}

bool IsDrawableLike(vector<uint32_t> const & types, GeomType geomType)
{
  Classificator const & c = classif();

  for (uint32_t t : types)
  {
    if (c.GetObject(t)->IsDrawableLike(geomType, false /* emptyName */))
      return true;
  }
  return false;
}

bool IsDrawableForIndex(FeatureType & ft, int level)
{
  return IsDrawableForIndexGeometryOnly(ft, level) &&
         IsDrawableForIndexClassifOnly(TypesHolder(ft), level);
}

bool IsDrawableForIndex(TypesHolder const & types, m2::RectD limitRect, int level)
{
  return IsDrawableForIndexGeometryOnly(types, limitRect, level) &&
         IsDrawableForIndexClassifOnly(types, level);
}

bool IsDrawableForIndexGeometryOnly(FeatureType & ft, int level)
{
  return IsDrawableForIndexGeometryOnly(TypesHolder(ft),
                                        ft.GetLimitRect(FeatureType::BEST_GEOMETRY), level);
}
bool IsDrawableForIndexGeometryOnly(TypesHolder const & types, m2::RectD limitRect, int level)
{
  Classificator const & c = classif();

  static uint32_t const buildingPartType = c.GetTypeByPath({"building:part"});

  if (types.GetGeomType() == GeomType::Area && !types.Has(c.GetCoastType()) &&
      !types.Has(buildingPartType) && !scales::IsGoodForLevel(level, limitRect))
    return false;

  return true;
}

bool IsDrawableForIndexClassifOnly(TypesHolder const & types, int level)
{
  Classificator const & c = classif();
  for (uint32_t t : types)
  {
    if (TypeAlwaysExists(t) || c.GetObject(t)->IsDrawable(level))
      return true;
  }

  return false;
}

bool IsUsefulType(uint32_t t, GeomType geomType, bool emptyName)
{
  Classificator const & c = classif();

  if (IsUsefulNondrawableType(t, geomType))
    return true;

  ClassifObject const * obj = c.GetObject(t);
  CHECK(obj, ());

  if (obj->IsDrawableLike(geomType, emptyName))
    return true;

  // IsDrawableLike checks only unique area styles, so we need to take into account point styles too.
  if (geomType == GeomType::Area)
  {
    if (obj->IsDrawableLike(GeomType::Point, emptyName))
      return true;
  }

  return false;
}

bool HasUsefulType(vector<uint32_t> const & types, GeomType geomType, bool emptyName)
{
  return any_of(types.begin(), types.end(), [&](uint32_t t)
  {
    return IsUsefulType(t, geomType, emptyName);
  });
}

bool RemoveUselessTypes(vector<uint32_t> & types, GeomType geomType, bool emptyName)
{
  base::EraseIf(types, [&] (uint32_t t)
  {
    return !IsUsefulType(t, geomType, emptyName);
  });

  return !types.empty();
}

int GetMinDrawableScale(FeatureType & ft)
{
  return GetMinDrawableScale(TypesHolder(ft), ft.GetLimitRect(FeatureType::BEST_GEOMETRY));
}

int GetMinDrawableScale(TypesHolder const & types, m2::RectD limitRect)
{
  int const upBound = scales::GetUpperStyleScale();

  for (int level = 0; level <= upBound; ++level)
  {
    if (IsDrawableForIndex(types, limitRect, level))
      return level;
  }

  return -1;
}

int GetMinDrawableScaleGeometryOnly(TypesHolder const & types, m2::RectD limitRect)
{
  int const upBound = scales::GetUpperStyleScale();

  for (int level = 0; level <= upBound; ++level)
  {
    if (IsDrawableForIndexGeometryOnly(types, limitRect, level))
      return level;
  }

  return -1;
}

int GetMinDrawableScaleClassifOnly(TypesHolder const & types)
{
  int const upBound = scales::GetUpperStyleScale();

  for (int level = 0; level <= upBound; ++level)
  {
    if (IsDrawableForIndexClassifOnly(types, level))
      return level;
  }

  return -1;
}

namespace
{
  void AddRange(pair<int, int> & dest, pair<int, int> const & src)
  {
    if (src.first != -1)
    {
      ASSERT_GREATER(src.first, -1, ());
      ASSERT_GREATER(src.second, -1, ());

      dest.first = min(dest.first, src.first);
      dest.second = max(dest.second, src.second);

      ASSERT_GREATER(dest.first, -1, ());
      ASSERT_GREATER(dest.second, -1, ());
    }
  }

  pair<int, int> kInvalidScalesRange(-1, -1);
} // namespace

pair<int, int> GetDrawableScaleRange(uint32_t type)
{
  auto const res = classif().GetObject(type)->GetDrawScaleRange();
  return (res.first > res.second ? kInvalidScalesRange : res);
}

pair<int, int> GetDrawableScaleRange(TypesHolder const & types)
{
  pair<int, int> res(1000, -1000);

  for (uint32_t t : types)
    AddRange(res, GetDrawableScaleRange(t));

  return (res.first > res.second ? kInvalidScalesRange : res);
}

bool IsVisibleInRange(uint32_t type, pair<int, int> const & scaleRange)
{
  CHECK_LESS_OR_EQUAL(scaleRange.first, scaleRange.second, (scaleRange));
  if (TypeAlwaysExists(type))
    return true;

  ClassifObject const * obj = classif().GetObject(type);
  CHECK(obj, ());

  for (int scale = scaleRange.first; scale <= scaleRange.second; ++scale)
  {
    if (obj->IsDrawable(scale))
      return true;
  }
  return false;
}

namespace
{
  bool IsDrawableForRules(TypesHolder const & types, int level, int rules)
  {
    Classificator const & c = classif();

    IsDrawableRulesChecker doCheck(level, types.GetGeomType(), rules);
    for (uint32_t t : types)
    {
      if (doCheck(c.GetObject(t)))
        return true;
    }

    return false;
  }
} // namespace

pair<int, int> GetDrawableScaleRangeForRules(TypesHolder const & types, int rules)
{
  int const upBound = scales::GetUpperStyleScale();
  int lowL = -1;
  for (int level = 0; level <= upBound; ++level)
  {
    if (IsDrawableForRules(types, level, rules))
    {
      lowL = level;
      break;
    }
  }

  if (lowL == -1)
    return kInvalidScalesRange;

  int highL = lowL;
  for (int level = upBound; level > lowL; --level)
  {
    if (IsDrawableForRules(types, level, rules))
    {
      highL = level;
      break;
    }
  }

  return {lowL, highL};
}

TypeSetChecker::TypeSetChecker(initializer_list<char const *> const & lst)
{
  m_type = classif().GetTypeByPath(lst);
  m_level = base::checked_cast<uint8_t>(lst.size());
}

bool TypeSetChecker::IsEqual(uint32_t type) const
{
  ftype::TruncValue(type, m_level);
  return (m_type == type);
}
}   // namespace feature
