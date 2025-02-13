#include "AmpGen/TreePhaseSpace.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/ParticleProperties.h"
#include "AmpGen/Utilities.h"
#include "AmpGen/Event.h"
#include "TRandom.h"
#include "TRandom3.h"
#include <numeric>
#include <random>

using namespace AmpGen;

TreePhaseSpace::TreePhaseSpace(const Particle& decayChain, const EventType& type, TRandom* rndm) :
  m_rand(rndm == nullptr ? (TRandom3*)gRandom : (TRandom3*)rndm),
  m_type(type)
{
  auto orderings = decayChain.identicalDaughterOrderings();
  for( auto& ordering : orderings )
  {
    Particle p = decayChain;
    p.setOrdering(ordering);
    m_top.push_back( Vertex::make( p) );
    m_weights.push_back(1);
    double w_max = m_top.rbegin()->maxWeight();
    if( w_max > m_wmax ) m_wmax = w_max;  
  }
  m_wmax = 1./m_wmax; 
  double sum_of_weights = std::accumulate( m_weights.begin(), m_weights.end(), 0 );
  m_dice = std::discrete_distribution<>(m_weights.begin(), m_weights.end()); 
  for( auto& w : m_weights ) w /= sum_of_weights; 
  setRandom(m_rand);
}

TreePhaseSpace::TreePhaseSpace(const std::vector<Particle>& decayChains, const EventType& type, TRandom* rndm) :
  m_rand(rndm == nullptr ? (TRandom3*)gRandom : (TRandom3*)rndm),
  m_type(type)
{
  for( auto& decayChain : decayChains )
  {
    auto orderings = decayChain.identicalDaughterOrderings();
    for( auto& ordering : orderings )
    {
      Particle p = decayChain;
      p.setOrdering(ordering);
      m_top.push_back( Vertex::make(p) );
      m_weights.push_back(1);
      double w_max = m_top.rbegin()->maxWeight();
      if( w_max > m_wmax ) m_wmax = w_max;  
    }
  }
  m_wmax = 1./m_wmax; 
  setRandom(rndm);
  double sum_of_weights = std::accumulate( m_weights.begin(), m_weights.end(), 0 );
  for( auto& w : m_weights ) w /= sum_of_weights; 
  m_dice = std::discrete_distribution<>(m_weights.begin(), m_weights.end()); 
}

Event TreePhaseSpace::makeEvent()
{
  unsigned j = 0; 
  double w = 0;
  int trials = 0;  
  do {
    j = m_dice(m_gen); 
    m_top[j].generate();
    w = m_top[j].weight() * m_wmax;
    if( trials++ >= 100000 ){ ERROR("Failed to generate a point with PHSP 100000," << m_wmax) ; break; } 
 //   if( trials++ % 100000 == 0  && trials != 0  ) WARNING("Tried " << trials << " events, still nada, " << w << " " << m_wmax );
  } while ( w < m_rand->Uniform() );
  //INFO("Made event after ... " << trials << " trials");
  auto event = m_top[j].event(m_type.size());
  event.setGenPdf( genPdf(event) );
  return event; 
}

void TreePhaseSpace::provideEfficiencyReport(const std::vector<bool>& report){}

double rho( const double& s, const double& s1, const double& s2)
{
  return sqrt( 1 - 2 * (s1+s2)/s + (s1-s2)*(s1-s2) /(s*s) );
}

TreePhaseSpace::Vertex::Vertex(const Particle& particle, const double& mass) 
    : particle(particle)
    , min(mass)
    , max(mass)
    , type( Type::Stable )
    , index(particle.index())
    , bwMass(particle.props()->mass())
    , bwWidth(particle.props()->width())
    , s(bwMass*bwMass)
{
  if( index != 999 ) indices = {index};
}
  
  
TreePhaseSpace::Vertex::Vertex(const Particle& particle, const double& min, const double& max ) 
    : particle(particle)
    , index(particle.index()) 
    , bwMass(particle.props()->mass())
    , bwWidth(particle.props()->width())
    , min(min)
    , max(max)
    , s(bwMass*bwMass)
{
  //INFO( particle << " [" << min << ", " << max << "]");
  if( particle.isStable() ) type = Type::Stable; 
  else if( particle.isQuasiStable() ) type = Type::QuasiStable; 
  else if( particle.lineshape().find("BW") != std::string::npos ){
    type = Type::BW; 
    phiMin = atan((min*min - bwMass*bwMass)/(bwMass*bwWidth));
    phiMax = atan((max*max - bwMass*bwMass)/(bwMass*bwWidth));
  }
  else type = Type::Flat;
  if( index != 999 ) indices = {index};
}

double TreePhaseSpace::Vertex::p() const 
{ 
  return 0.5 * sqrt( s - 2 * (left->s+right->s) + (left->s-right->s)*(left->s-right->s)/s ); 
}
 
double TreePhaseSpace::Vertex::weight() const 
{
  if( left == nullptr || right == nullptr ) return 1.0;
  double w = double( sqrt(s) - sqrt(left->s) - sqrt(right->s) > 0 ); 
  if( w == 0 ) return 0;
  w *= rho(s, left->s, right->s);
  w *= left  -> weight();
  w *= right -> weight();
  return w;
}

double TreePhaseSpace::Vertex::genPdf(const Event& event) const 
{
  if( left == nullptr || right == nullptr ) return 1;
  double dp = left->genPdf(event) * right->genPdf(event); 
  auto st = event.s(indices);
  switch( type ) {
    case Type::BW :
      dp *= ( bwMass* bwWidth ) /( (phiMax-phiMin) * ( (st - bwMass*bwMass)*(st-bwMass*bwMass) + bwMass*bwMass*bwWidth*bwWidth) );
      break;
    case Type::Flat : 
      dp *= 1/(max*max -min*min);
      break;
  };
  return dp;
}

void TreePhaseSpace::Vertex::generate() 
{
  switch( type ) {
    case Type::BW :
      s = bwMass * bwMass + bwMass * bwWidth * tan( (phiMax - phiMin ) * rand->Rndm() +  phiMin );
      break;
    case Type::Flat : 
      s = (max*max-min*min)*rand->Rndm() + min * min;
      break;
  };
  if(  left != nullptr )  left->generate();
  if( right != nullptr ) right->generate();
}

void TreePhaseSpace::Vertex::print(const unsigned& offset) const 
{
  std::array<const char*, 4> vtxTypeStrings = {"BW", "Flat", "Stable", "QuasiStable"};
  INFO( std::string(offset,' ') << particle.name() << " [" << vectorToString(indices, ", ") << "] type = " << vtxTypeStrings[ type ] << " → [" << min << ", " << max << "] " << sqrt(s) );
  if( type == Type::BW )
    INFO( "phi-range : " << phiMin << " " << phiMax 
        << " s(min) = " << bwMass * bwMass + bwMass * bwWidth * tan(phiMin) 
        << " s(max) = " << bwMass * bwMass + bwMass * bwWidth * tan(phiMax) << " " << bwMass << " " << bwWidth );
  if( left  != nullptr ) left  -> print( offset + 4 );
  if( right != nullptr ) right -> print( offset + 4 );
}

void TreePhaseSpace::Vertex::place(Event& event)
{
  if( index != 999 ){
    event[4*index+0] = mom[0];
    event[4*index+1] = mom[1];
    event[4*index+2] = mom[2];
    event[4*index+3] = mom[3];
  }
  if( left != nullptr ) left->place(event);
  if( right != nullptr ) right->place(event);
}

Event TreePhaseSpace::Vertex::event(const unsigned& eventSize)
{
  Event output(4 * eventSize); 
  mom.SetXYZT(0,0,0,sqrt(s)); 
  generateFullEvent();
  place(output); 
  return output;
}

void TreePhaseSpace::Vertex::generateFullEvent()
{ 
  if( left == nullptr || right == nullptr ) return;
  double cosTheta = 2 * rand->Rndm() - 1;
  double sinTheta = sqrt( 1 - cosTheta * cosTheta );
  double angY     = 2 * M_PI * rand->Rndm();
  double cosPhi   = cos(angY);
  double sinPhi   = sin(angY);
  double pf = p();
  if( std::isnan(pf) || std::isnan(s) )
  {
    auto p2 = ( s - 2 * (left->s+right->s) + (left->s-right->s)*(left->s-right->s)/s ); 
    ERROR("Generating nan: " << pf << " " << s << " " << min << " " << max << " " << p2 << " " << left->s << " " << right->s << " w = " << weight() );
  }
  left  -> mom.SetXYZT(  pf*sinTheta*cosPhi,  pf*sinTheta*sinPhi,  pf*cosTheta, sqrt(left->s + pf*pf) );
  left  -> mom.Boost( mom.BoostVector() );
  left  -> generateFullEvent();
  
  right -> mom.SetXYZT( -pf*sinTheta*cosPhi, -pf*sinTheta*sinPhi, -pf*cosTheta, sqrt(right->s + pf*pf) );
  right -> mom.Boost( mom.BoostVector() );
  right -> generateFullEvent();
} 

TreePhaseSpace::Vertex TreePhaseSpace::Vertex::make(const Particle& particle, TreePhaseSpace::Vertex* parent)
{
  auto decayProducts = particle.daughters();
  auto threshold     = [](const Particle& particle)
  {
    auto d1 = particle.getFinalStateParticles();
    if( d1.size() == 0) return particle.mass();
    return std::accumulate(d1.begin(), d1.end(), 0., [](double acc, auto& p){ return acc + p->mass(); } );
  };
  if( decayProducts.size() == 0 ) return TreePhaseSpace::Vertex(); 
  if( decayProducts.size() == 1 ) return TreePhaseSpace::Vertex::make(*decayProducts[0], parent);
  if( decayProducts.size() == 2 )
  {
    double G = particle.isQuasiStable() ? 0 : particle.props()->width() * 10;
    TreePhaseSpace::Vertex vtx = (parent == nullptr) ? TreePhaseSpace::Vertex(particle, particle.mass() - G , particle.mass() + G) : TreePhaseSpace::Vertex(); 
    parent = ( parent == nullptr ) ? &vtx : parent; 
    auto min_mass_1 = threshold(*decayProducts[0]); 
    auto min_mass_2 = threshold(*decayProducts[1]); 
    parent->left   = std::make_shared<TreePhaseSpace::Vertex>(*decayProducts[0], min_mass_1, parent->max - min_mass_2);
    parent->right  = std::make_shared<TreePhaseSpace::Vertex>(*decayProducts[1], min_mass_2, parent->max - min_mass_1);
    TreePhaseSpace::Vertex::make(*decayProducts[0], parent->left.get());
    TreePhaseSpace::Vertex::make(*decayProducts[1], parent->right.get());
    for( auto& index : parent->left ->indices ) parent->indices.push_back(index);
    for( auto& index : parent->right->indices ) parent->indices.push_back(index);
    return *parent;  
  }
  else {
    /// split using quasi particles
    std::vector<Particle> particles;
    for( unsigned int i = 1; i != decayProducts.size(); ++i ) particles.push_back( *decayProducts[i] );
    Particle particle2( "NonResS0", particles );
    return TreePhaseSpace::Vertex::make( Particle( particle.name(), *decayProducts[0], particle2 ), parent);
  }
  return TreePhaseSpace::Vertex();
}

void TreePhaseSpace::setRandom( TRandom* rand )
{
  m_rand = (TRandom3*)rand;
  for( auto& channel : m_top ) channel.setRandom((TRandom3*)rand); 
}

void TreePhaseSpace::Vertex::setRandom( TRandom3* rnd )
{
  rand = rnd;
  if( left  != nullptr ) left->setRandom(rnd);
  if( right != nullptr ) right->setRandom(rnd);
}

EventType TreePhaseSpace::eventType() const 
{
  return m_type; 
}

double TreePhaseSpace::genPdf(const Event& event) const 
{
  double genPdf = 0; 
  for( unsigned i = 0; i != m_top.size(); ++i ) 
    genPdf += m_weights[i] * m_top[i].genPdf(event);
  return genPdf;
}

size_t TreePhaseSpace::size() const 
{
  return m_top.size();
}


double TreePhaseSpace::Vertex::maxWeight() const 
{
  if( left == nullptr || right == nullptr ) return 1.0;
  return rho(max*max, left->min * left->min, right->min * right->min) * left->maxWeight() * right->maxWeight(); 
}

