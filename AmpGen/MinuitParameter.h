#ifndef AMPGEN_MINUITPARAMETER_H
#define AMPGEN_MINUITPARAMETER_H
// author: Jonas Rademacker (Jonas.Rademacker@bristol.ac.uk)
// status:  Mon 9 Feb 2009 19:17:55 GMT

#include <iostream>
#include <string>
#include "AmpGen/enum.h"

namespace AmpGen
{
  class MinuitParameterSet;
  declare_enum( Flag, Free, Hide, Fix, CompileTimeConstant, Blind)
  class MinuitParameter
  {
  public: 

    MinuitParameter() = default;
    MinuitParameter(const std::string& name, const Flag& flag, const double& mean, const double& step,
                     const double& min = 0, const double& max = 0 );
    MinuitParameter(const std::string& name, const double& mean, const double& step,
                     const double& min = 0, const double& max = 0 );

    Flag flag() const;
    bool isFixed() const;
    bool isFree()  const;
    bool isBlind()  const;
    const std::string& name() const;

    double meanInit() const;
    double stepInit() const;
    double minInit() const;
    double maxInit() const;
    double err() const;
    double errPos() const;
    double errNeg() const;
    double* vp() { return &m_meanResult ; } 
    
    void setInit( const double& init, const double& step=-1 );
    void setStepInit( const double& si );
    void setFree() ;
    void scaleStep( const double& sf );
    void fix();
    void setCurrentFitVal( double cfv );
    void setLimits( const double& min, const double& max );
    void setResult( double fitMean, double fitErr, double fitErrNeg, double fitErrPos );
    void resetToInit();
    void setName( const std::string& name );
    virtual double mean() const;
    virtual operator double() const { return m_meanResult; }
    virtual ~MinuitParameter() = default;
    
    void setFromMinuitState( const double* x ){ if( m_minuitIndex != -1 ) m_meanResult = x[m_minuitIndex] ; } 
    void setMinuitIndex( const int& index ){ m_minuitIndex = index; }  
    int index() const { return m_minuitIndex; } 
  
    friend class MinuitParameterSet;
  protected:
    Flag m_flag;
    std::string m_name = {""};
    double m_meanInit = {0};
    double m_stepInit = {0};
    double m_minInit  = {0};
    double m_maxInit  = {0};
    double m_meanResult = {0};
    double m_errPosResult = {0};
    double m_errNegResult = {0};
    double m_errResult    = {0};
    int    m_minuitIndex  = {-1}; 
  };

  class MinuitProxy
  {
  public:
    void update() { if(m_parameter != nullptr ) m_value = m_parameter->mean(); }
    MinuitParameter* ptr() { return m_parameter; }
    operator double() const { return m_parameter == nullptr ? m_value : m_parameter->mean(); }
    MinuitProxy(MinuitParameter* param = nullptr, const double& value=0) : m_parameter(param), m_value(value) { update(); }
    MinuitParameter* operator->() { return m_parameter; }
    const MinuitParameter* operator->() const { return m_parameter; }
  private:
    MinuitParameter* m_parameter{nullptr};
    double m_value;
  };
  std::ostream& operator<<( std::ostream& os, const MinuitParameter& type );
} // namespace AmpGen

#endif
//
