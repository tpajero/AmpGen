#include "AmpGen/Vertex.h"

#include <stddef.h>
#include <array>
#include <memory>
#include <ostream>
#include <initializer_list>
#include <cmath>
#include <complex>

#include "AmpGen/DiracMatrices.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/Units.h"

using namespace AmpGen;
using namespace std::complex_literals;


static const Tensor::Index mu    = Tensor::Index();
static const Tensor::Index nu    = Tensor::Index();
static const Tensor::Index alpha = Tensor::Index();
static const Tensor::Index beta  = Tensor::Index();
static const Tensor::Index a     = Tensor::Index();
static const Tensor::Index b     = Tensor::Index(); 
static const Tensor::Index c     = Tensor::Index(); 
static const Tensor::Index d     = Tensor::Index(); 

template <> Factory<AmpGen::Vertex::Base>* Factory<AmpGen::Vertex::Base>::gImpl = nullptr;

bool Vertex::Factory::isVertex( const std::string& hash ) { return get( hash ) != nullptr; }

Tensor Vertex::Factory::getSpinFactor( const Tensor& P, const Tensor& Q, const Tensor& V1, const Tensor& V2,
    const std::string& name, DebugSymbols* db )
{
  auto connector = Vertex::Factory::get( name );
  if ( connector == nullptr ) {
    ERROR( "Vertex: " << name << " not yet implemented"); 
    ERROR( "Try to use canonical spin formulation: " << italic_on << "Particle::SpinFormalism canonical" << italic_off ); 
    FATAL( "Vertex: " << name << " not yet implemented."); 
    return Tensor( std::vector<double>( {1.} ), {0} );
  } else
    return (*connector)( P, Q, V1, V2, db );
}

Tensor Vertex::Factory::getSpinFactorNBody( const std::vector<std::pair<Tensor, Tensor>>& tensors, const unsigned int& mL,
    DebugSymbols* db )
{
  if ( tensors.size() != 3 ) {
    ERROR( "N-Body spin tensors only implemented for specific three-body decays, please check logic" );
    return Tensor( std::vector<double>( {1.} ), {1} );
  }
  if ( mL == 1 )
    return LeviCivita()( -mu, -nu, -alpha, -beta ) *
      tensors[0].first( nu ) * tensors[1].first( alpha ) * tensors[2].first( beta ); 
  else if ( mL == 0 )
    return Tensor( std::vector<double>( {1} ), {1} );
  else
    return Tensor( std::vector<double>( {1.} ), {1} );
}

const Tensor Metric4x4(){
  return Tensor( {-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}, {4, 4} );
}

Tensor AmpGen::Orbital_PWave(const Tensor& p, const Tensor& q)
{ 
  auto is = 1./make_cse( dot(p,p) ,true);
  return q - p * make_cse( dot(p, q) * is );
}


Tensor AmpGen::Orbital_DWave(const Tensor& p, const Tensor& q)
{
  Tensor L = Orbital_PWave(p, q);
  Tensor f =  L(mu) * L(nu) - make_cse( dot( L, L ) / 3. ) * Spin1Projector(p) ( mu, nu );
  f.imposeSymmetry(0,1); 
  f.st();
  return f;  
}

Tensor AmpGen::Spin1Projector( const Tensor& P )
{
  auto is = 1./make_cse( dot(P,P) , true);
  return Metric4x4()(mu, nu) - P(mu) * P(nu) * is;
}

Tensor AmpGen::Spin2Projector( const Tensor& P )
{
  Tensor S = Spin1Projector( P );
  Tensor SP =  -( 1. / 3. ) * S( mu, nu ) * S( alpha, beta ) + (1./2.) * ( S( mu, alpha ) * S( nu, beta ) + S( mu, beta ) * S( nu, alpha ) ) ;
  return SP;
}


Tensor AmpGen::Gamma4Vec()
{
  Tensor x( {1, 0, 0, 0}, {4} );
  Tensor y( {0, 1, 0, 0}, {4} );
  Tensor z( {0, 0, 1, 0}, {4} );
  Tensor t( {0, 0, 0, 1}, {4} );
  return x( mu ) * Gamma[0]( a, b ) + y( mu ) * Gamma[1]( a, b ) + z( mu ) * Gamma[2]( a, b ) + t( mu ) * Gamma[3]( a, b );
}

Tensor AmpGen::slash( const Tensor& P )
{
  if ( P.dims() != std::vector<unsigned>({4}) ) {
    ERROR( "Can only compute slash operator against vector currents" );
    return Tensor();
  }
  return Gamma4Vec()( mu, a, b ) * P( -mu );
}

Tensor AmpGen::gamma_twiddle( const Tensor& P )
{
  Tensor g = Gamma4Vec();
  return g(nu,a,b) - P(nu) * P(mu) * g(-mu,a,b) / make_cse( dot(P,P) );
}

Tensor AmpGen::Spin1hProjector( const Tensor& P )
{
  Expression m = make_cse( fcn::sqrt( dot( P, P ) ) );
  return 0.5 * ( slash(P)/m + Identity() );
}

Tensor AmpGen::Spin3hProjector( const Tensor& P )
{
  Tensor Ps = P;
  Ps.st();
  Tensor g = gamma_twiddle(Ps);
  Tensor F = Spin1hProjector(Ps);
  Tensor S = Spin1Projector(Ps);
  S.imposeSymmetry(0,1);
  Tensor rt = (-1.) * S(mu,nu) * F(a,b) + (1./3.) * F(a,c) * g(mu,c,d) * g(nu,d,b);
  rt.st(true);
  return rt;
}

Tensor AmpGen::Bar( const Tensor& P ){
  return P.conjugate()(b) * Gamma[3](b,a);
}

DEFINE_VERTEX( S_SS_S ) { return V1 * V2[0]; }

DEFINE_VERTEX( S_VV_S ) { return V1( mu ) * V2( -mu ); }
DEFINE_VERTEX( S_VV_S1 ) { return Spin1Projector(P)(mu,nu) * V1( -mu ) * V2( -nu ); }

DEFINE_VERTEX( S_VV_D )
{
  Tensor L2   = Orbital_DWave(P, Q);
  return V1(mu)*L2(-mu,-nu)*V2(nu);
}

DEFINE_VERTEX( S_VV_P )
{
  Tensor L        = Orbital_PWave( P, Q );
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * L( alpha ) * P( beta );
  return V1( mu ) * coupling( -mu, -nu ) * V2( nu ); 
}

DEFINE_VERTEX( S_VS_P )
{
  Tensor p_wave = Orbital_PWave( P, Q );
  Tensor p_v1   = V1( mu ) * p_wave( -mu );
  return p_v1 * V2[0];
}

DEFINE_VERTEX( V_SS_P ){ return Orbital_PWave(P,Q) * V1[0] * V2[0];}

DEFINE_VERTEX( V_VS_P )
{
  Tensor  L        = Orbital_PWave( P, Q ); 
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * L( alpha ) * P( beta );
  return ( coupling(mu,nu) * V1( -nu ) ) * V2[0];
}


DEFINE_VERTEX( V_VS_S ){ auto S = Spin1Projector(P)(mu,nu) * V1(-nu) * V2[0]; ADD_DEBUG_TENSOR(S,db); return S; }

DEFINE_VERTEX( V_VS_D )
{
  Tensor L_2_V0 = Orbital_DWave( P, Q ); 
  Tensor Sv     = Spin1Projector( P );
  return ( Sv( mu, nu ) * L_2_V0( -nu, -alpha ) * V1( alpha ) ) * V2[0];
}

DEFINE_VERTEX( V_VV_P )
{
  Tensor L = Orbital_PWave( P, Q );
  return L(mu) * dot(V1,V2);
}

DEFINE_VERTEX( V_VV_P1 )
{
   Tensor L = Orbital_PWave( P, Q );
   Tensor Sv  = Spin1Projector( P );
   Tensor term = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( nu )  * V1( alpha ) * V2( beta );
   Tensor phi_1 = term(-mu) * Sv(mu,nu);
      
   return LeviCivita()( -mu, -nu, -alpha, -beta ) * P( mu ) * L(nu) * phi_1(alpha);
}

DEFINE_VERTEX( V_VV_P2 )
{
    Tensor L = Orbital_PWave( P, Q ); 
    Tensor Sv  = Spin2Projector( P );
    Tensor phi_2 = Sv(-mu,-nu,alpha,beta) * V1(mu) * V2(nu);
        
    return L(-mu) * phi_2(mu,nu);
}

DEFINE_VERTEX( V_VV_S )
{
    Tensor Sv  = Spin1Projector( P );
    Tensor term = LeviCivita()( -mu, -nu, -alpha, -beta ) * P(nu) * V1( alpha ) * V2( beta );
        
    return term( -mu ) * Sv(mu, nu);
}


DEFINE_VERTEX( T_VS_D )
{
  Tensor G = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( nu ) * Q( alpha ) * V1( beta );
  Tensor L = Orbital_PWave( P, Q );
  return -0.5 * ( G( mu ) * L( nu ) + L( mu ) * G( nu ) ) * V2[0];
}

DEFINE_VERTEX( T_TS_S )
{
  Tensor S     = Spin1Projector( P );
  Tensor term1 = S( -mu, -alpha ) * V1( alpha, beta ) * S( -beta, -nu );
  Tensor term2 = S * ( dot( V1, S ) ) / 3.;
  return ( term1 - term2 ) * V2[0];
}

DEFINE_VERTEX( T_VS_P )
{
  Tensor L  = Orbital_PWave( P, Q );
  Tensor S  = Spin1Projector( P );
  Tensor Vp = S( -mu, -nu ) * V1( nu );
  return V2[0] * ( ( L( alpha ) * Vp( beta ) + L( beta ) * Vp( alpha ) ) / 2. - S( alpha, beta ) * dot( L, V1 ) / 3. );
}

DEFINE_VERTEX( T_SS_D ) { return Orbital_DWave( P, Q )  * V1[0] * V2[0]; }

DEFINE_VERTEX( S_TV_P )
{
  Tensor L = Orbital_PWave( P, Q );
  return ( V1( mu, nu ) * L( -mu ) ) * V2( -nu );
}

DEFINE_VERTEX( S_TS_D )
{
  Tensor orbital = Orbital_DWave( P, Q );
  return V2[0] * Tensor( {dot( orbital, V1 )}, {1} );
}

DEFINE_VERTEX( S_TV_D )
{
  Tensor term1 = V1( alpha, beta ) * Orbital_DWave( P, Q )( -beta, -nu );
  Tensor term2 = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( alpha ) * V2( beta );
  return Tensor( {dot( term1, term2 )} );
}

DEFINE_VERTEX( S_TT_S ) { return Tensor( {dot( V1, V2 )} ); }

DEFINE_VERTEX( V_TS_P )
{
  Tensor S = Spin1Projector( P );
  Tensor L = Orbital_PWave( P, Q );
  return ( S( -mu, -nu ) * L( -alpha ) * V1( nu, alpha ) ) * V2[0];
}

DEFINE_VERTEX( V_TS_D )
{
  Tensor L        = ( -1 ) * Orbital_PWave( P, Q );
  Tensor coupling = LeviCivita()( -mu, -nu, -alpha, -beta ) * P( nu ) * Q( alpha );
  return coupling( -mu, -nu ) * V1( nu, alpha ) * L( -alpha );
}

DEFINE_VERTEX( f_fS_S )
{
  Tensor f_fS_Sv = Spin1hProjector(P)(a,b) * V1(b) * V2[0];
  ADD_DEBUG_TENSOR( f_fS_Sv, db);
  return f_fS_Sv; 
}

DEFINE_VERTEX( f_fS_P )
{
  Tensor proj   = Spin1hProjector(P);
  Tensor L      = Orbital_PWave(P,Q);
  ADD_DEBUG_TENSOR( P, db );
  ADD_DEBUG_TENSOR( Q, db );
  ADD_DEBUG_TENSOR( L, db );
  Tensor t      = proj(a, b) * Gamma[4](b,c) * slash(L)(c,d) * V1(d);
  return t; 
}

DEFINE_VERTEX( f_Vf_S )
{
  Tensor proj   = Spin1hProjector(P);
  return proj(a, b) * Gamma[4](b,c) * gamma_twiddle(P)(mu,c,d) * V2(d) * V1(-mu);
}

DEFINE_VERTEX( f_Vf_P ) /// = A3 
{
  Tensor proj   = Spin1hProjector(P);
  auto L = Orbital_PWave(P,Q);
  return proj( alpha, beta ) * V2(beta) * dot( V1, L );
}

DEFINE_VERTEX( f_Vf_P1 ) //// = A6
{
  Tensor proj   = Spin1hProjector(P);
  Tensor L = Orbital_PWave(P,Q);
  Tensor t = LeviCivita()(mu,nu,alpha,beta) * V1(-nu) * L(-alpha) * P(-beta); 
  t.st();
  return proj( a, b ) * Gamma[4](b,c) * gamma_twiddle(P)(mu,c,d) * V2(d) * t(-mu);
}

DEFINE_VERTEX( f_Vf_D ) //// = A8 
{
  Tensor proj   = Spin1hProjector(P);
  Tensor gt     = gamma_twiddle(P);
  Tensor L      = Orbital_DWave(P,Q)(-mu,-nu) * V1(nu); 
  return proj( a, b ) * Gamma[4](b,c) * gt(mu,c,d) * V2(d) * L(-mu);
}

DEFINE_VERTEX( f_Vf_S1 )
{
  Tensor proj   = Spin1hProjector(P);
  Tensor vSlash = gamma_twiddle(P)(mu,a,b) * V1(-mu);
  return proj( a, b ) * vSlash(b,c) * V2(c);
}

DEFINE_VERTEX( f_Vf_SL )
{ 
  Tensor proj = Spin1hProjector(P);
  return proj(a, b) * Gamma4Vec()(mu,b,c) * ( Identity(4) - Gamma[4] )(c,d)* V2(d) * V1(-mu);
}

DEFINE_VERTEX( f_Vf_SR )
{ 
  Tensor proj = Spin1hProjector(P);
  return proj(a, b) * Gamma4Vec()(mu,b,c) * ( Identity(4) + Gamma[4] )(c,d)* V2(d) * V1(-mu);
}

DEFINE_VERTEX( f_fS_SL )
{ 
  Tensor proj = Spin1hProjector(P);
  return proj(a, b) * ( Identity(4) - Gamma[4] )(b,c)* V2(c);
}


DEFINE_VERTEX( f_fS_SR )
{ 
  Tensor proj = Spin1hProjector(P);
  return proj(a, b) * ( Identity(4) + Gamma[4] )(b,c)* V2(c);
}

DEFINE_VERTEX( f_fS_S1 ){ return Spin1hProjector(P)(a,b) * Gamma[4](b,c) * V1(c) * V2[0];}

DEFINE_VERTEX( f_fS_P1 )
{
  return Spin1hProjector(P)(a, b) * slash(Orbital_PWave(P,Q))(b,c) * V1(c) * V2[0];
}

DEFINE_VERTEX( f_Tf_P )
{
  Tensor proj   = Spin1hProjector(P);
  Tensor T = V1;
  T.imposeSymmetry(0,1);
  T.st();
  return proj( a, b ) * gamma_twiddle(P)(mu,b,c) * V2(c) * T(-mu,-nu) * Orbital_PWave(P,Q)(nu);
}

DEFINE_VERTEX( f_Vf_P2 ) //A4 
{
  Tensor proj   = Spin1hProjector(P);
  return proj( alpha, beta ) * Gamma[4](beta,nu) * V2(nu) * dot( V1, Orbital_PWave( P, Q ) );
}

DEFINE_VERTEX( f_Vf_P3 ) //A5
{
  Tensor proj   = Spin1hProjector(P);
  Tensor L = Orbital_PWave(P,Q);
  Tensor t = LeviCivita()(-mu,-nu,-alpha,-beta) * L(nu) * V1(alpha) * P(beta); 
  t.st();
  return proj( a, b ) * gamma_twiddle(P)(mu,b,c) * V2(c) * t(-mu); 
}


DEFINE_VERTEX( f_Vf_D1 )
{
  Tensor proj   = Spin1hProjector(P);
  Tensor gt     = gamma_twiddle(P);
  Tensor L      = Orbital_DWave(P,Q)(-mu,-nu) * V1(nu); 
  return proj( a, b ) * gt(mu,b,c) * L(-mu) * V2(c) ;
}


DEFINE_VERTEX( r_fS_P )
{
  Tensor L = Orbital_PWave(P,Q);
  L.st();
  Tensor F = Spin1hProjector(P);
  Tensor V = ( (-1) * L(nu) * F(a,d) + (1./3.) * F(a,b) * gamma_twiddle(P)(nu,b,c) * slash(L)(c,d) ) * V1(d); 
  V.st();
  return V;
}

DEFINE_VERTEX( r_fS_D )
{
  Tensor::Index e;
  Tensor L = Orbital_PWave(P,Q);
  Tensor F = Spin1hProjector(P);
  L.st(true);
  Expression L2 = make_cse(dot(L,L));
  Tensor gt = gamma_twiddle(P);
  Tensor rt =  ( L(mu) * F(a,b) * slash(L)(b,c) - (L2/3.) * F(a,b) * gt(mu,b,c) ) * Gamma[4](c,d) * V1(d); 
  rt.st();
  return rt;
}


DEFINE_VERTEX( f_rS_D )
{
  Tensor F = Spin1hProjector(P)(a,b) 
    * Gamma[4](b,d) 
    * gamma_twiddle(P)(mu,d,c)
    * V1(alpha,c) 
    * Orbital_DWave(P,Q)(-mu,-alpha) 
    * V2[0];
  F.st();
  return F;
}

DEFINE_VERTEX( f_rS_P )
{
  auto L = Orbital_PWave(P,Q);
  Tensor F = Spin1hProjector(P)(a,b)                 * V1(-mu,b) * L(mu) * V2[0];
  ADD_DEBUG_TENSOR( V1, db );
  ADD_DEBUG_TENSOR( L , db );
  ADD_DEBUG_TENSOR( F , db );
  F.st();
  return F;
}

DEFINE_VERTEX( f_rS_P1 )
{
  auto L = Orbital_PWave(P,Q);
  Tensor F = Spin1hProjector(P)(a,b) * Gamma[4](b,c) * V1(-mu,c) * L(mu) * V2[0]; 
  F.st();
  return F;
}

DEFINE_VERTEX( S_ff_S ) { return Bar(V2)(a) * V1(a); }

DEFINE_VERTEX( S_ff_S1 ){ return Bar(V2)(a) * Gamma[4](a,b) * V1(b); }

DEFINE_VERTEX( V_ff_S ) { return Bar(V2)(a) * Gamma4Vec()(mu,a,b) * V1(b); }

DEFINE_VERTEX( V_ff_S1 ){ return Bar(V2)(a) * Gamma[4](a,b) * Gamma4Vec()(mu,b,c) * V1(c); }

DEFINE_VERTEX( V_ff_PL )
{ 
  Tensor proj = Spin1Projector(P);
  auto pl = 0.5 * ( Identity(4) - Gamma[4] ); 
  ADD_DEBUG_TENSOR( pl, db);
  ADD_DEBUG_TENSOR( pl(b,c)* V2(c), db );
  return proj(mu, nu) * Bar(V1)(a) * Gamma4Vec()(-nu,a,b) * ( Identity(4) - Gamma[4] )(b,c)* V2(c); 
}

DEFINE_VERTEX( V_ff_PR )
{ 
  Tensor proj = Spin1Projector(P);
  ADD_DEBUG_TENSOR( (Identity(4) + Gamma[4] )(b,c)* V2(c), db );
  return proj(mu, nu) * Bar(V1)(a) * Gamma4Vec()(-nu,a,b) * ( Identity(4) + Gamma[4] )(b,c)* V2(c); 
}



DEFINE_VERTEX( S_VV_rp ){
  // P = Pres; Q = Pgamma, V1 = PolVector_gamma, V2 = polVector_Kres

  // Implementation for B to Kres gamma with 1+ Kres spin-parity 
  // Inputs: P = Pres+Pgamma; Q = Pres - Pgamma, V1 = PolVector_Kres, V2 = polVector_gamma

  Tensor P1 = 0.5 * (P+Q);//Pkres
  Tensor P2 = 0.5 * (P-Q);//Pgamma

  auto pP = fcn::sqrt(P2[0]*P2[0] + P2[1]*P2[1] + P2[2]*P2[2]);
  Tensor helOp = 1i* (SO3[0]*P2[0] + SO3[1]*P2[1] + SO3[2]*P2[2]) / pP; //helicity operator iL.p
  Tensor helOpV2 = helOp(mu,nu)*V2(nu);

  Tensor dot1 = Tensor( {dot(V2,V1) * dot(P1,P2)} );
  Tensor dot2 = Tensor( {dot(V2,P1) * dot(V1,P2)} );
  Tensor prod = Tensor( {LeviCivita()(-mu,-nu,-alpha,-beta)*helOpV2(mu)*V1(nu)*P1(alpha)*P2(beta)},{1} );
  
  return -1i*prod + (dot1-dot2);
} 

DEFINE_VERTEX( S_VV_rm ){
  // Implementation for B to Kres gamma with 1- Kres spin-parity 
  // Inputs: P = Pres+Pgamma; Q = Pres - Pgamma, V1 = PolVector_Kres, V2 = polVector_gamma

  Tensor P1 = 0.5 * (P+Q);//Pkres
  Tensor P2 = 0.5 * (P-Q);//Pgamma

  auto pP = fcn::sqrt(P2[0]*P2[0] + P2[1]*P2[1] + P2[2]*P2[2]);
  Tensor helOp = 1i* (SO3[0]*P2[0] + SO3[1]*P2[1] + SO3[2]*P2[2]) / pP; //helicity operator iL.p
  Tensor helOpV2 = helOp(mu,nu)*V2(nu);

  Tensor dot1 = Tensor( {dot(helOpV2,V1) * dot(P1,P2)} );
  Tensor dot2 = Tensor( {dot(helOpV2,P1) * dot(V1,P2)} );
  Tensor prod = Tensor( {LeviCivita()(-mu,-nu,-alpha,-beta)*V2(mu)*V1(nu)*P1(alpha)*P2(beta)},{1} );
  
  return -1i*prod + (dot1-dot2);
}


DEFINE_VERTEX( S_TV_rp ){
  // Implementation for B to Kres gamma with 2+ Kres spin-parity 
  Tensor P1 = 0.5 * (P+Q);//Pkres
  Tensor P2 = 0.5 * (P-Q);//Pgamma

  auto pP = fcn::sqrt(P2[0]*P2[0] + P2[1]*P2[1] + P2[2]*P2[2]);
  Tensor helOp = 1i* (SO3[0]*P2[0] + SO3[1]*P2[1] + SO3[2]*P2[2]) / pP; //helicity operator iL.p
  Tensor helOpV2 = helOp(mu,nu)*V2(nu);
  helOpV2.st();

  auto dot0 = make_cse(dot(P1,P2));// P1(alpha) * P2(-alpha);
  Tensor res = V1(mu,nu)*P1(-nu);
  res.st();

  return -1i*LeviCivita()(-mu,-nu,-alpha,-beta)*V2(mu)*res(nu)*P1(alpha)*P2(beta) 
      + helOpV2(mu)*(V1(-mu,-nu)*dot0-P1(-mu)*res(-nu))*P2(nu);
} 


DEFINE_VERTEX( S_TV_rm ){
  // Implementation for B to Kres gamma with 2- Kres spin-parity 
  Tensor P1 = 0.5 * (P+Q);//Pkres
  Tensor P2 = 0.5 * (P-Q);//Pgamma

  auto pP = fcn::sqrt(P2[0]*P2[0] + P2[1]*P2[1] + P2[2]*P2[2]);
  Tensor helOp = 1i* (SO3[0]*P2[0] + SO3[1]*P2[1] + SO3[2]*P2[2]) / pP; //helicity operator iL.p
  Tensor helOpV2 = helOp(mu,nu)*V2(nu);
  helOpV2.st();

  auto dot0 = make_cse(dot(P1,P2));//  P1(alpha) * P2(-alpha);
  Tensor res = V1(mu,nu)*P1(-nu);
  res.st();

  return -1i*LeviCivita()(-mu,-nu,-alpha,-beta)*helOpV2(mu)*res(nu)*P1(alpha)*P2(beta) 
      + V2(mu)*(V1(-mu,-nu)*dot0-P1(-mu)*res(-nu))*P2(nu);
} 

