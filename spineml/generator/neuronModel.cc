#include "neuronModel.h"

// Standard C++ includes
#include <iostream>
#include <sstream>

// Standard C includes
#include <cstring>

// pugixml includes
#include "pugixml/pugixml.hpp"

// SpineML common includes
#include "spineMLUtils.h"

// Spine ML generator includes
#include "modelParams.h"
#include "objectHandler.h"

using namespace SpineMLCommon;

//----------------------------------------------------------------------------
// Anonymous namespace
//----------------------------------------------------------------------------
namespace
{
//----------------------------------------------------------------------------
// ObjectHandlerNeuronCondition
//----------------------------------------------------------------------------
class ObjectHandlerNeuronCondition : public SpineMLGenerator::ObjectHandler::Condition
{
public:
    ObjectHandlerNeuronCondition(SpineMLGenerator::CodeStream &codeStream, const std::string &sendPortSpike)
        : SpineMLGenerator::ObjectHandler::Condition(codeStream), m_SendPortSpike(sendPortSpike){}

    //----------------------------------------------------------------------------
    // SpineMLGenerator::ObjectHandler::Condition virtuals
    //----------------------------------------------------------------------------
    void onObject(const pugi::xml_node &node, unsigned int currentRegimeID,
                  unsigned int targetRegimeID)
    {
        // Superclass
        SpineMLGenerator::ObjectHandler::Condition::onObject(node, currentRegimeID,
                                                           targetRegimeID);

        // If this condition emits a spike
        // **TODO** also handle spike like event clause
        pugi::xpath_variable_set spikeEventsOutVars;
        spikeEventsOutVars.set("portName", m_SendPortSpike.c_str());
        auto spikeEventOut = node.select_node("EventOut[@port=$portName]", &spikeEventsOutVars);
        if(spikeEventOut) {
            // Add current regime and trigger condition to map
            // **NOTE** cannot build code immediately as we don't know if there are multiple regimes
            auto triggerCode = node.child("Trigger").child("MathInline");
            if(!m_RegimeThresholds.insert(std::make_pair(currentRegimeID, triggerCode.text().get())).second) {
                throw std::runtime_error("Only one spike trigger is supported per regime");
            }
        }
    }

    //----------------------------------------------------------------------------
    // Public API
    //----------------------------------------------------------------------------
    std::string getThresholdCode(bool multipleRegimes) const
    {
        // If there are multiple regimes
        std::ostringstream thresholdCodeStream;
        if(multipleRegimes) {
            // Loop through them
            for(const auto &r : m_RegimeThresholds) {
                // If there are existing threshold conditions, OR them with this one
                if(thresholdCodeStream.tellp() > 0) {
                    thresholdCodeStream << " || ";
                }
                
                // Add test, ANDing test for correct regime ID with threshold condition
                thresholdCodeStream << "(_regimeID == " << r.first << " && (" << r.second << "))";
            }
        }
        // Otherwise, if there are any threshold tests
        else if(!m_RegimeThresholds.empty()) {
            if(m_RegimeThresholds.size() > 1) {
                throw std::runtime_error("Multiple regimes have not been found but there are thresholds specified for different regimes");
            }

            // Code should simple be that test
            thresholdCodeStream << "(" << m_RegimeThresholds.begin()->second << ")";
        }
        return thresholdCodeStream.str();

    }

private:
    //----------------------------------------------------------------------------
    // Members
    //----------------------------------------------------------------------------
    std::map<unsigned int, std::string> m_RegimeThresholds;
    std::string m_SendPortSpike;
};
}

//----------------------------------------------------------------------------
// SpineMLGenerator::NeuronModel
//----------------------------------------------------------------------------
SpineMLGenerator::NeuronModel::NeuronModel(const ModelParams::Neuron &params)
{
    // Load XML document
    pugi::xml_document doc;
    auto result = doc.load_file(params.getURL().c_str());
    if(!result) {
        throw std::runtime_error("Could not open file:" + params.getURL() + ", error:" + result.description());
    }

    // Get SpineML root
    auto spineML = doc.child("SpineML");
    if(!spineML) {
        throw std::runtime_error("XML file:" + params.getURL() + " is not a SpineML component - it has no root SpineML node");
    }

    // Get component class
    auto componentClass = spineML.child("ComponentClass");
    if(!componentClass || strcmp(componentClass.attribute("type").value(), "neuron_body") != 0) {
        throw std::runtime_error("XML file:" + params.getURL() + " is not a SpineML 'neuron_body' component - "
                                 "it's ComponentClass node is either missing or of the incorrect type");
    }

    // Loop through send ports
    std::cout << "\t\tSend ports:" << std::endl;
    std::map<std::string, std::string> sendPortAliases;
    for(auto sendPort : componentClass.select_nodes(SpineMLUtils::xPathNodeHasSuffix("SendPort").c_str())) {
        std::string nodeType = sendPort.node().name();
        const char *portName = sendPort.node().attribute("name").value();
        if(nodeType == "AnalogSendPort") {
            // If there is an alias matching this port name, add alias code to map to resolve later
            std::string aliasCode;
            if(findAlias(componentClass, portName, aliasCode)) {
                std::cout << "\t\t\tImplementing analogue send port '" << portName << "' as an alias" << std::endl;
                sendPortAliases.insert(std::make_pair(portName, aliasCode));
            }
            else {
                std::cout << "\t\t\tImplementing analogue send port '" << portName << "' using a GeNN model variable" << std::endl;
            }

            // Add send port to set
            m_SendPortVariables.insert(portName);
        }
        else if(nodeType == "EventSendPort") {
            if(m_SendPortSpike.empty()) {
                std::cout << "\t\t\tImplementing event send port '" << portName << "' as a GeNN spike" << std::endl;
                m_SendPortSpike = portName;
            }
            else {
                std::cout << "\t\t\tImplementing event send port '" << portName << "' as a GeNN spike-like-event" << std::endl;
                m_SendPortSpikeLikeEvent = portName;
                throw std::runtime_error("Spike-like event sending not currently implemented");
            }
        }
        else {
            throw std::runtime_error("GeNN does not support '" + nodeType + "' send ports in neuron models");
        }
    }

    // Check that there are no unhandled receive ports
    std::cout << "\t\tReceive ports:" << std::endl;
    for(auto receivePort : componentClass.select_nodes(SpineMLUtils::xPathNodeHasSuffix("ReceivePort").c_str())) {
        std::string nodeType = receivePort.node().name();
        const char *portName = receivePort.node().attribute("name").value();

        if(nodeType == "AnalogReceivePort") {
            std::cout << "\t\t\tImplementing analogue receive port '" << portName << "' as GeNN additional input variable" << std::endl;
            m_AdditionalInputVars.push_back(std::make_pair(portName, std::make_pair("scalar", 0.0)));
        }
        else {
            throw std::runtime_error("GeNN does not support '" + nodeType + "' reduce ports in neuron models");
        }
    }

    // Loop through reduce ports
    std::cout << "\t\tReduce ports:" << std::endl;
    for(auto reducePort : componentClass.select_nodes(SpineMLUtils::xPathNodeHasSuffix("ReducePort").c_str())) {
        std::string nodeType = reducePort.node().name();
        const char *portName = reducePort.node().attribute("name").value();

        // **TODO** implement other reduce operations
        if(nodeType == "AnalogReducePort" && strcmp(reducePort.node().attribute("reduce_op").value(), "+") == 0) {
            std::cout << "\t\t\tImplementing analogue reduce port '" << portName << "' as GeNN additional input variable" << std::endl;
            m_AdditionalInputVars.push_back(std::make_pair(portName, std::make_pair("scalar", 0.0)));
        }
        else {
            throw std::runtime_error("GeNN does not support '" + nodeType + "' reduce ports in neuron models");
        }
    }

    // Create a code stream for generating sim code
    CodeStream simCodeStream;

    // Create lambda function to end regime on all code streams when required
    auto regimeEndFunc =
        [&simCodeStream]
        (bool multipleRegimes, unsigned int currentRegimeID)
        {
            simCodeStream.onRegimeEnd(multipleRegimes, currentRegimeID);
        };

    // Generate model code using specified condition handler
    ObjectHandlerNeuronCondition objectHandlerCondition(simCodeStream, m_SendPortSpike);
    ObjectHandler::TimeDerivative objectHandlerTimeDerivative(simCodeStream);
    const bool multipleRegimes = generateModelCode(componentClass, {}, &objectHandlerCondition,
                                                   {}, &objectHandlerTimeDerivative,
                                                   regimeEndFunc);

    // Loop through send ports which send an alias
    auto variableParams = params.getVariableParams();
    for(const auto &s : sendPortAliases) {
        // Add simulation code to calculate send port value and store in state variable
        simCodeStream << s.first << " = " << s.second << ";" << std::endl;

        // Add this state variable to variable params set
        variableParams.insert(s.first);
    }

    // Store generated code in class
    m_SimCode = simCodeStream.str();
    m_ThresholdConditionCode = objectHandlerCondition.getThresholdCode(multipleRegimes);

    // Build the final vectors of parameter names and variables from model and
    // correctly wrap references to them in newly-generated code strings
    tie(m_ParamNames, m_Vars) = processModelVariables(componentClass, variableParams,
                                                      multipleRegimes, {&m_SimCode, &m_ThresholdConditionCode});
}