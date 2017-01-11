#include "routing/index_graph_starter.hpp"

#include "routing/routing_exceptions.hpp"

namespace routing
{
// static
Segment constexpr IndexGraphStarter::kStartFakeSegment;
Segment constexpr IndexGraphStarter::kFinishFakeSegment;

IndexGraphStarter::IndexGraphStarter(IndexGraph & graph, FakeVertex const & start,
                                     FakeVertex const & finish)
  : m_graph(graph), m_start(start), m_finish(finish)
{
}

m2::PointD const & IndexGraphStarter::GetPoint(Segment const & segment, bool front)
{
  if (segment == kStartFakeSegment || (!front && m_start.Fits(segment)))
    return m_start.GetPoint();

  if (segment == kFinishFakeSegment || (front && m_finish.Fits(segment)))
    return m_finish.GetPoint();

  return m_graph.GetGeometry().GetPoint(segment.GetRoadPoint(front));
}

// static
size_t IndexGraphStarter::GetRouteNumPoints(vector<Segment> const & segments)
{
  // Valid route contains at least 3 segments:
  // start fake, finish fake and at least one normal nearest segment.
  CHECK_GREATER_OR_EQUAL(segments.size(), 3, ());

  // -2 for fake start and finish.
  // +1 for front point of first segment.
  return segments.size() - 1;
}

m2::PointD const & IndexGraphStarter::GetRoutePoint(vector<Segment> const & segments,
                                                    size_t pointIndex)
{
  if (pointIndex == 0)
    return m_start.GetPoint();

  CHECK_LESS(pointIndex, segments.size(), ());
  return GetPoint(segments[pointIndex], true /* front */);
}

void IndexGraphStarter::GetOutgoingEdgesList(TVertexType const & segment, vector<TEdgeType> & edges)
{
  GetEdgesList(segment, true /* isOutgoing */, edges);
}

void IndexGraphStarter::GetIngoingEdgesList(TVertexType const & segment, vector<TEdgeType> & edges)
{
  GetEdgesList(segment, false /* isOutgoing */, edges);
}

void IndexGraphStarter::GetEdgesList(Segment const & segment, bool isOutgoing,
                                     vector<SegmentEdge> & edges)
{
  edges.clear();

  if (segment == kStartFakeSegment)
  {
    GetFakeToNormalEdges(m_start, edges);
    return;
  }

  if (segment == kFinishFakeSegment)
  {
    GetFakeToNormalEdges(m_finish, edges);
    return;
  }

  m_graph.GetEdgeList(segment, isOutgoing, edges);
  GetNormalToFakeEdge(segment, m_start, kStartFakeSegment, isOutgoing, edges);
  GetNormalToFakeEdge(segment, m_finish, kFinishFakeSegment, isOutgoing, edges);
}

void IndexGraphStarter::GetFakeToNormalEdges(FakeVertex const & fakeVertex,
                                             vector<SegmentEdge> & edges)
{
  GetFakeToNormalEdge(fakeVertex, true /* forward */, edges);

  if (!m_graph.GetGeometry().GetRoad(fakeVertex.GetFeatureId()).IsOneWay())
    GetFakeToNormalEdge(fakeVertex, false /* forward */, edges);
}

void IndexGraphStarter::GetFakeToNormalEdge(FakeVertex const & fakeVertex, bool forward,
                                            vector<SegmentEdge> & edges)
{
  Segment const segment(fakeVertex.GetFeatureId(), fakeVertex.GetSegmentIdx(), forward);
  RoadPoint const & roadPoint = segment.GetRoadPoint(true /* front */);
  m2::PointD const & pointTo = m_graph.GetGeometry().GetPoint(roadPoint);
  double const weight = m_graph.GetEstimator().CalcHeuristic(fakeVertex.GetPoint(), pointTo);
  edges.emplace_back(segment, weight);
}

void IndexGraphStarter::GetNormalToFakeEdge(Segment const & segment, FakeVertex const & fakeVertex,
                                            Segment const & fakeSegment, bool isOutgoing,
                                            vector<SegmentEdge> & edges)
{
  if (!fakeVertex.Fits(segment))
    return;

  m2::PointD const & pointFrom = m_graph.GetGeometry().GetPoint(segment.GetRoadPoint(isOutgoing));
  double const weight = m_graph.GetEstimator().CalcHeuristic(pointFrom, fakeVertex.GetPoint());
  edges.emplace_back(fakeSegment, weight);
}
}  // namespace routing
