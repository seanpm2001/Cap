#define BOOST_TEST_MODULE TestCyclicVoltammetry
#define BOOST_TEST_MAIN
#include <cap/resistor_capacitor.h>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <fstream>



namespace cap {

void report(double const t, std::shared_ptr<cap::EnergyStorageDevice const> dev, std::ostream & os = std::cout)
{
    os<<boost::format("%10.5f  ") % t;
    dev->print_data(os);
}



void scan(std::shared_ptr<cap::EnergyStorageDevice> dev, std::shared_ptr<boost::property_tree::ptree const> database, std::ostream & os = std::cout)
{
    double const scan_rate           = database->get<double>("scan_rate"          );
    double const step_size           = database->get<double>("step_size"          );
    double const initial_voltage     = database->get<double>("initial_voltage"    );
    double const upper_voltage_limit = database->get<double>("upper_voltage_limit");
    double const lower_voltage_limit = database->get<double>("lower_voltage_limit");
    double const final_voltage       = database->get<double>("final_voltage"      );
    int    const cycles              = database->get<int   >("cycles"             );

    double const time_step = step_size / scan_rate;
    double time = 0.0;
    dev->reset_voltage(initial_voltage);
    for (int n = 0; n < cycles; ++n)
    {
        double voltage = initial_voltage;
        for ( ; voltage <= upper_voltage_limit; voltage += step_size, time+=time_step)
        {
            dev->evolve_one_time_step_constant_voltage(time_step, voltage);
            report(time, dev, os);
        }
        for ( ; voltage >= lower_voltage_limit; voltage -= step_size, time+=time_step)
        {
            dev->evolve_one_time_step_constant_voltage(time_step, voltage);
            report(time, dev, os);
        }
        for ( ; voltage <= final_voltage; voltage += step_size, time+=time_step)
        {
            dev->evolve_one_time_step_constant_voltage(time_step, voltage);
            report(time, dev, os);
        }
    }
}

} // end namespace cap

BOOST_AUTO_TEST_CASE( test_cyclic_voltammetry )
{
    // parse input file
    std::shared_ptr<boost::property_tree::ptree> input_database =
        std::make_shared<boost::property_tree::ptree>();
    read_xml("input_cyclic_voltammetry", *input_database);

    // build an energy storage system
    std::shared_ptr<boost::property_tree::ptree> device_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("device"));
    std::shared_ptr<cap::EnergyStorageDevice> device =
        cap::buildEnergyStorageDevice(std::make_shared<cap::Parameters>(device_database));

    // scan the system
    std::fstream fout;
    fout.open("cyclic_voltammetry_data", std::fstream::out);

    std::shared_ptr<boost::property_tree::ptree> cyclic_voltammetry_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("cyclic_voltammetry"));
    cap::scan(device, cyclic_voltammetry_database, fout);
}    