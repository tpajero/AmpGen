#include "AmpGen/CompiledExpressionBase.h"

#include <cxxabi.h>
#include <algorithm>
#include <memory>
#include <ostream>

#include "AmpGen/CacheTransfer.h"
#include "AmpGen/NamedParameter.h"
#include "AmpGen/Utilities.h"
#include "AmpGen/ThreadPool.h"
#include "AmpGen/CompilerWrapper.h"
#include "AmpGen/ASTResolver.h"
#include "AmpGen/ProfileClock.h"
#include "AmpGen/Tensor.h"
#include "AmpGen/simd/utils.h"
using namespace AmpGen;

CompiledExpressionBase::CompiledExpressionBase() = default; 

CompiledExpressionBase::CompiledExpressionBase( const std::string& name ) : 
  m_name( name ),
  m_progName( programatic_name(name) ) {}

CompiledExpressionBase::CompiledExpressionBase( const Expression& expression, 
    const std::string& name, 
    const DebugSymbols& db,
    const std::map<std::string, unsigned>& evtMapping ) : 
  m_obj( expression ), 
  m_name( name ),
  m_progName( programatic_name(name) ),
  m_db(db),
  m_evtMap(evtMapping) {}

CompiledExpressionBase::~CompiledExpressionBase() {}

std::string AmpGen::programatic_name( std::string s )
{
  std::replace( s.begin(), s.end(), '-', 'm' );
  std::replace( s.begin(), s.end(), '+', 'p' );
  std::replace( s.begin(), s.end(), '-', 'm' );
  std::replace( s.begin(), s.end(), '*', 's' );
  std::replace_if( s.begin(), s.end(), [](auto& c){ return ! std::isalnum(c) ; }, '_' );
  if( isdigit( s[0] ) ) s = "f" + s;
  std::replace( s.begin(), s.end(), '\'', '_' );
  return s;
}

void CompiledExpressionBase::resolve(const MinuitParameterSet* mps)
{
  if( m_resolver == nullptr ) m_resolver = std::make_shared<ASTResolver>( m_evtMap, mps );
  if( fcnSignature().find("AVX") != std::string::npos ) m_resolver->setEnableAVX();   
  m_dependentSubexpressions = m_resolver->getOrderedSubExpressions( m_obj ); 
  for ( auto& sym : m_db ){
    auto expressions_for_this = m_resolver->getOrderedSubExpressions( sym.second); 
    for( auto& it : expressions_for_this ){
      auto is_same = [&it](const auto& jt){ return it.first == jt.first ; };
      if( !std::any_of( m_debugSubexpressions.begin(), m_debugSubexpressions.end(), is_same)  ) m_debugSubexpressions.push_back( it );
    }
  }
  m_cacheTransfers.clear();
  for( auto& [expr, fcache] : m_resolver->cacheFunctions() ) 
    m_cacheTransfers.emplace_back( fcache );
  DEBUG("Resizing cache to: " << m_resolver->nParams() ); 
  resizeExternalCache( m_resolver->nParams() ); 
  prepare(); 
}

std::string CompiledExpressionBase::name() const { return m_name; }
std::string CompiledExpressionBase::progName() const { return m_progName; }
unsigned int CompiledExpressionBase::hash() const { return FNV1a_hash(m_name); }

void CompiledExpressionBase::prepare()
{
  for ( auto& t : m_cacheTransfers ) t->transfer( this );
}

void CompiledExpressionBase::addDependentExpressions( std::ostream& stream, size_t& sizeOfStream ) const
{
  for ( auto& dep : m_dependentSubexpressions ) {
    std::string rt = "  const auto v" + std::to_string(dep.first) + " = " + dep.second.to_string(m_resolver.get()) +";"; 
    stream << rt << "\n";
    sizeOfStream += sizeof(char) * rt.size(); /// bytes /// 
  }
}

void CompiledExpressionBase::to_stream( std::ostream& stream  ) const 
{
  if( m_db.size() !=0 ) stream << "#include<iostream>\n"; 
  stream << "extern \"C\" const char* " << progName() << "_name() {  return \"" << m_name << "\"; } \n";
  bool enable_cuda = NamedParameter<bool>("UseCUDA",false);
  size_t sizeOfStream = 0;
  if( use_rto() )
  {
    
    stream << "extern \"C\" " << returnTypename() << " " << progName() << "(" << fcnSignature() << "){\n";
    addDependentExpressions( stream , sizeOfStream );\
    std::string return_type = arg_type(0);
    return_type = return_type.substr(0,return_type.size()-1);
    if( is<TensorExpression>(m_obj) == false ){
      stream << "*r = " << return_type << "(" << m_obj.to_string(m_resolver.get()) << ");\n";
     stream << "}\n";
    }
    else {
      auto as_tensor = cast<TensorExpression>(m_obj).tensor(); 
      for(unsigned j=0; j != as_tensor.size(); ++j )
        stream << "r[s * "<< j<<"] = " << return_type << "(" << as_tensor[j].to_string(m_resolver.get()) << ");\n";
      stream << "}\n";  
    }
  }
  else if( !enable_cuda ){
    stream << "extern \"C\" " << returnTypename() << " " << progName() << "(" << fcnSignature() << "){\n";
    addDependentExpressions( stream , sizeOfStream );
    stream << "return " << returnTypename() << "(" << m_obj.to_string(m_resolver.get()) << ");\n}\n";
  }
  else {
    stream << "__global__ void " << progName() << "( " << returnTypename() + "* r, const int N, " << fcnSignature() << "){\n"; 
    stream <<  "  int i     = blockIdx.x * blockDim.x + threadIdx.x;\n";
    addDependentExpressions( stream, sizeOfStream);
    stream << "  r[i] = " << m_obj.to_string(m_resolver.get()) << ";\n}\n";
  }

  if( NamedParameter<bool>("IncludePythonBindings", false) == true && returnTypename().find("complex") != std::string::npos ){
    stream << "extern \"C\" void " <<  progName() << "_c" << "(double *real, double *imag, " << fcnSignature() << "){\n";
    stream << "  auto val = " << progName() << "(" << args() << ") ;\n"; 
    stream << "  *real = val.real();\n";
    stream << "  *imag = val.imag();\n";
    stream << "}\n";
  }
  if ( m_db.size() != 0 ) addDebug( stream );
  if( !m_disableBatch ) compileBatch(stream);    
}

std::ostream& AmpGen::operator<<( std::ostream& os, const CompiledExpressionBase& expression )
{
  expression.to_stream(os);
  return os; 
}

void CompiledExpressionBase::compile(const std::string& fname)
{
  CompilerWrapper(false).compile(*this,fname );
}

void CompiledExpressionBase::addDebug( std::ostream& stream ) const
{
  stream << "#include<string>\n";
  stream << "extern \"C\" std::vector<std::pair< std::string, " << type_string<complex_v>() << " >> " 
         << m_progName << "_DB(" << fcnSignature() << "){\n";
  for ( auto& dep : m_debugSubexpressions ) {
    std::string rt = "auto v" + std::to_string(dep.first) + " = " + dep.second.to_string(m_resolver.get()) +";"; 
    stream << rt << "\n";
  }
  stream << "return {";
  for ( unsigned int i = 0 ; i < m_db.size(); ++i ) {
    std::string comma = (i!=m_db.size()-1)?", " :"};\n}\n";
    const auto expression = m_db[i].second; 
    stream << std::endl << "{\"" << m_db[i].first << "\",";
    if ( expression.to_string(m_resolver.get()) != "NULL" )
      stream << type_string<complex_v>() << "("<< expression.to_string(m_resolver.get()) << ")}" << comma;
    else stream << type_string<complex_v>() << "(-999.,0.)}" << comma ;
  }
}

std::string CompiledExpressionBase::fcnSignature(const std::vector<std::string>& argList, bool rto, bool includeStagger)
{
  unsigned counter=0;
  auto fcn = [counter](const auto& str) mutable {return str + " x"+std::to_string(counter++); }; 
  if( rto ) return argList[0] + " r, " + argList[1] + " s, " + vectorToString( argList.begin()+2, argList.end(), ", ", fcn );
  return vectorToString( argList.begin(), argList.end(), ", ", fcn);
}

std::vector<const CacheTransfer*> CompiledExpressionBase::orderedCacheFunctors() const 
{ 
  std::vector<const CacheTransfer*> ordered_cache_functors; 
  for( const auto& c : m_cacheTransfers ) ordered_cache_functors.push_back( c.get() );
  std::sort( ordered_cache_functors.begin(),
                     ordered_cache_functors.end(),
                     [](auto& c1, auto& c2 ) { return c1->address() < c2->address() ; } );
  return ordered_cache_functors; 
}
