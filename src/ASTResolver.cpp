#include "AmpGen/ASTResolver.h"

#include <algorithm>

#include "AmpGen/MinuitParameterSet.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/Spline.h"
#include "AmpGen/Array.h"
#include "AmpGen/MinuitParameter.h"
#include "AmpGen/MsgService.h"
#include "AmpGen/ExpressionParser.h"

using namespace AmpGen;

ASTResolver::ASTResolver(const std::map<std::string, unsigned>& evtMap, 
    const MinuitParameterSet* mps ) : 
  m_evtMap(evtMap),
  m_mps(mps), 
  m_nParameters(0)
{
  m_enable_cuda                 = NamedParameter<bool>("UseCUDA",false);
  m_enable_compileTimeConstants = NamedParameter<bool>("ASTResolver::CompileTimeConstants", false);
  m_check_hashes                = NamedParameter<bool>("ASTResolver::CheckHashes", false );
}

std::vector<std::pair<uint64_t,Expression>> ASTResolver::getOrderedSubExpressions( const Expression& expression )
{
  DEBUG("Calling solver on: " << expression );
  std::vector<std::pair<uint64_t, Expression>> subexpressions; 
  expression.resolve( *this );
  std::map<uint64_t, size_t> used_functions; 
  std::map<uint64_t, Expression> subTrees;
  do { 
    subTrees.clear();
    for( auto& [t, s] : m_tempTrees )
    {
      uint64_t key = s->key(); 
      if( subTrees.count( key ) == 0 ) subTrees[ key ] = s->expression();
      else if( m_check_hashes && t->to_string() != subTrees[key].to_string() )
      {
        WARNING("Hash collision between in key = " << key << " other key = " << FNV1a_hash( subTrees[key].to_string() ) );
      }
    }
    m_tempTrees.clear(); 
    for( auto& [key,expression] : subTrees ){
      expression.resolve( *this );
      auto stack_pos = used_functions.find(key);
      if ( stack_pos == used_functions.end() ) {
        subexpressions.emplace_back(key , expression );
        used_functions[key] = subexpressions.size() - 1;
        continue;
      }
      auto oldPos = stack_pos->second;
      auto it     = subexpressions.begin() + oldPos;
      if ( it == subexpressions.end() - 1 ) continue;
      std::rotate( it, it + 1, subexpressions.end() );
      for ( auto uf = used_functions.begin(); uf != used_functions.end(); ++uf ) {
        if ( uf->second >= oldPos ) uf->second = uf->second - 1;
      }
      used_functions[key] = subexpressions.size() - 1;
    }
  } while ( subTrees.size() !=0 );
  std::reverse( subexpressions.begin(), subexpressions.end() );
  return subexpressions;
}

template <> void ASTResolver::resolve<SubTree>( const SubTree& subTree )
{
  auto ptr = subTree.expression().get();
  if( m_tempTrees.count(ptr) != 0 ) return;
  else {
    ptr->resolve( *this );
    m_tempTrees[ptr] = &subTree;
  }
}

template <> void ASTResolver::resolve<Spline>( const Spline& spline )
{ 
  if( m_resolvedParameters.count( &spline) != 0  ) return ; 
  auto address = addCacheFunction<SplineTransfer>(spline.m_name, spline.m_nKnots, spline.m_min, spline.m_max);
  addResolvedParameter( &spline, address );
  addResolvedParameter( spline.m_points.top().get(), address );  
  auto splineTransfer = dynamic_cast<SplineTransfer*>( m_cacheFunctions[spline.m_name].get() );
  if( m_mps == nullptr ) ERROR("Fix me: Spline parameters must come from a ParameterSet");
  for( unsigned int i = 0 ; i < spline.m_nKnots; ++i ) 
    splineTransfer->set(i, m_mps->find(spline.m_name+"::"+std::to_string(i)) );
}

template <> void ASTResolver::resolve<Parameter>( const Parameter& parameter )
{
  DEBUG( "Resolving: " << parameter.name() << " " << parameter.isResolved() );
  if( m_resolvedParameters.count(&parameter) != 0 || parameter.isResolved() ) return; 
  auto res = m_evtMap.find(parameter.name());
  if( res != m_evtMap.end() ){
    if( m_enable_cuda ) {
      size_t t = res->second; 
      std::string it = ""; 
      if( t % 3 == 0 ) it = ".x";
      if( t % 3 == 1 ) it = ".y";
      if( t % 3 == 2 ) it = ".z";
      int stg = t/3;
      std::string nTimesStg = "+"+std::to_string(stg) +"*N"; 
      if( stg == 0 ) nTimesStg = "";
      if( stg == 1 ) nTimesStg = "+N";
      addResolvedParameter( &parameter, "x1[i"+nTimesStg+"]" +it );
    }
    else {
      addResolvedParameter( &parameter, res->second ,1 );
    }
    return;
  }
  else if( m_mps != nullptr ){
    auto it = m_mps->find(parameter.name());
    if( it != nullptr ){
      if( m_enable_compileTimeConstants && it->flag() == Flag::CompileTimeConstant ){
        addResolvedParameter( &parameter, "("+std::to_string(it->mean()) +")" );
      }
      else addResolvedParameter( &parameter, addCacheFunction<ParameterTransfer>( parameter.name(), it )  );
      return;
    }
    DEBUG("Could not find parameter: " << parameter.name() << " amongst the MPS ");
  }
  else if( m_enable_compileTimeConstants ){
    addResolvedParameter( &parameter, std::to_string( parameter.defaultValue() ) );
    return;
  }
  auto address = addCacheFunction<CacheTransfer>( parameter.name(), parameter.defaultValue() );
  addResolvedParameter( &parameter, address );
  if( ! parameter.isResolved() ) m_unresolvedParameters.push_back( parameter ); 
}

template <> void ASTResolver::resolve<MinuitParameterLink>(const MinuitParameterLink& parameter)
{
  DEBUG( "Resolving: " << parameter.name() << " " << m_resolvedParameters.count(&parameter) );
  if( m_resolvedParameters.count(&parameter) != 0 ) return; 
  if( m_mps == nullptr ) return; 
  auto it = m_mps->find(parameter.name());
  if( it == nullptr ) return;
  addResolvedParameter(&parameter, addCacheFunction<ParameterTransfer>(parameter.name(), it));
}

template <> void ASTResolver::resolve<LambdaExpression>( const LambdaExpression& obj)
{
  if( m_resolvedParameters.count(&obj) != 0 ) return;
  addResolvedParameter(&obj, addCacheFunction<LambdaTransfer>( "lxpr_" + std::to_string(m_nParameters), &obj));
}

std::map<std::string, std::shared_ptr<CacheTransfer>> ASTResolver::cacheFunctions() const 
{ 
  return m_cacheFunctions;
}

void ASTResolver::addResolvedParameter(const IExpression* param, const std::string& thing) 
{
  m_resolvedParameters[param] = thing; 
}

void ASTResolver::addResolvedParameter(const IExpression* param, const size_t& address, const size_t& arg) 
{
  m_resolvedParameters[param] = "x"+std::to_string(arg)+"["+std::to_string(address)+"]";
}

std::string ASTResolver::resolvedParameter( const IExpression* param ) const
{
  auto it = m_resolvedParameters.find(param);
  if( it != m_resolvedParameters.end() ) return it->second; 
  else {
    ERROR( "Parameter cannot be resolved: [" << param <<"]: " << param->to_string() );
    return "";
  }
}

void ASTResolver::clear() 
{
  m_resolvedParameters.clear();
  m_cacheFunctions.clear();
  m_parameterMapping.clear();
  m_tempTrees.clear(); 
  m_nParameters = 0;
}
