/* Copyright (c) 2016, the Cap authors.
 *
 * This file is subject to the Modified BSD License and may not be distributed
 * without copyright and license information. Please refer to the file LICENSE
 * for the text and further information on this license. 
 */

#define BOOST_TEST_MODULE ExactTransientSolution
#define BOOST_TEST_MAIN
#include <cap/energy_storage_device.h>
#include <cap/mp_values.h>
#include <deal.II/base/types.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/dofs/dof_handler.h>
#include <boost/test/unit_test.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <cmath>
#include <iostream>
#include <fstream>
#include <numeric>

namespace cap {

void compute_parameters(std::shared_ptr<boost::property_tree::ptree const> input_database,
                        std::shared_ptr<boost::property_tree::ptree      > output_database)
{
    double const cross_sectional_area = 0.0001 * input_database->get<double>("geometry.geometric_area");
    double const electrode_width = 0.01 * input_database->get<double>("geometry.anode_electrode_thickness");
    double const separator_width = 0.01 * input_database->get<double>("geometry.separator_thickness");

    // getting the material parameters values
    std::shared_ptr<boost::property_tree::ptree> material_properties_database = 
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("material_properties"));
    cap::MPValuesParameters<2> mp_values_params(material_properties_database);
    std::shared_ptr<boost::property_tree::ptree> geometry_database = 
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("geometry"));
    mp_values_params.geometry = std::make_shared<cap::DummyGeometry<2>>(geometry_database);
    std::shared_ptr<cap::MPValues<2> > mp_values = std::shared_ptr<cap::MPValues<2> >
        (new cap::MPValues<2>(mp_values_params));
    // build dummy cell itertor and set its material id
    dealii::Triangulation<2> triangulation;
    dealii::GridGenerator::hyper_cube (triangulation);
    dealii::DoFHandler<2> dof_handler(triangulation);
    dealii::DoFHandler<2>::active_cell_iterator cell = 
        dof_handler.begin_active();
    // electrode
    cell->set_material_id(
        input_database->get<dealii::types::material_id>("geometry.anode_electrode_material_id"));
    std::vector<double> electrode_solid_electrical_conductivity_values(1);
    std::vector<double> electrode_liquid_electrical_conductivity_values(1);
    std::vector<double> electrode_specific_capacitance_values(1);
    std::vector<double> electrode_exchange_current_density_values(1);
    std::vector<double> electrode_electron_thermal_voltage_values(1);
    mp_values->get_values("solid_electrical_conductivity" , cell, electrode_solid_electrical_conductivity_values );
    mp_values->get_values("liquid_electrical_conductivity", cell, electrode_liquid_electrical_conductivity_values);
    mp_values->get_values("specific_capacitance"          , cell, electrode_specific_capacitance_values          );
    mp_values->get_values("faradaic_reaction_coefficient" , cell, electrode_exchange_current_density_values      );
    mp_values->get_values("electron_thermal_voltage"      , cell, electrode_electron_thermal_voltage_values      );
    if (electrode_exchange_current_density_values[0] == 0.0)
        throw std::runtime_error("test assumes faradaic processes are present, exchange_current_density has to be non zero");
    double const total_current = -1.0; // normalized
    double const dimensionless_exchange_current_density = electrode_exchange_current_density_values[0]
        * std::pow(electrode_width,2) 
        * ( 1.0 / electrode_solid_electrical_conductivity_values[0]
          + 1.0 / electrode_liquid_electrical_conductivity_values[0] );
    double const dimensionless_current_density = total_current
        * electrode_width
        / electrode_liquid_electrical_conductivity_values[0]
        / electrode_electron_thermal_voltage_values[0];
    double const ratio_of_solution_phase_to_matrix_phase_conductivities =
        electrode_liquid_electrical_conductivity_values[0] / electrode_solid_electrical_conductivity_values[0];

    output_database->put("dimensionless_current_density"                         , dimensionless_current_density                         );
    output_database->put("dimensionless_exchange_current_density"                , dimensionless_exchange_current_density                );
    output_database->put("ratio_of_solution_phase_to_matrix_phase_conductivities", ratio_of_solution_phase_to_matrix_phase_conductivities);

    output_database->put("position_normalization_factor", electrode_width);
    output_database->put("time_normalization_factor"    ,
        electrode_specific_capacitance_values[0] 
            * ( 1.0 / electrode_solid_electrical_conductivity_values[0]
              + 1.0 / electrode_liquid_electrical_conductivity_values[0] )
            * std::pow(electrode_width,2) );
          
    // separator
    cell->set_material_id(
        input_database->get<dealii::types::material_id>("geometry.separator_material_id"));
    std::vector<double> separator_liquid_electrical_conductivity_values(1);
    mp_values->get_values("liquid_electrical_conductivity", cell, separator_liquid_electrical_conductivity_values);

    double const potential_drop_across_the_separator = -total_current * separator_width / separator_liquid_electrical_conductivity_values[0];
    double const voltage_normalization_factor        = electrode_electron_thermal_voltage_values[0];
    output_database->put("potential_drop_across_the_separator", potential_drop_across_the_separator);
    output_database->put("voltage_normalization_factor"       , voltage_normalization_factor       );
    output_database->put("cross_sectional_area"               , cross_sectional_area               );
}



void verification_problem(std::shared_ptr<cap::EnergyStorageDevice> dev, std::shared_ptr<boost::property_tree::ptree const> database)
{
    double dimensionless_current_density                          = database->get<double>("dimensionless_current_density"                         );
    double dimensionless_exchange_current_density                 = database->get<double>("dimensionless_exchange_current_density"                );
    double ratio_of_solution_phase_to_matrix_phase_conductivities = database->get<double>("ratio_of_solution_phase_to_matrix_phase_conductivities");


    // gold vs computed
    double const charge_current = database->get<double>("charge_current");
    double const charge_time    = database->get<double>("charge_time"   );
    double const time_step      = database->get<double>("time_step"     );
    double const epsilon        = time_step * 1.0e-4;
    double const cross_sectional_area                = database->get<double>("cross_sectional_area"               );
    dimensionless_current_density *= charge_current / cross_sectional_area;

    std::cout<<"delta="<<dimensionless_current_density<<"\n";
    std::cout<<"nu2  ="<<dimensionless_exchange_current_density<<"\n";
    std::cout<<"beta ="<<ratio_of_solution_phase_to_matrix_phase_conductivities<<"\n";
    std::cout<<"time step = "<<time_step<<std::endl;

    unsigned int pos = 0;
    double computed_voltage;
    double const percent_tolerance = database->get<double>("percent_tolerance");
    std::vector<double> gold_solution(10);
    gold_solution[0] = 1.725914356067658e-01;
    gold_solution[1] = 1.802025636145941e-01;
    gold_solution[2] = 1.859326352495181e-01;
    gold_solution[3] = 1.905978440188036e-01;
    gold_solution[4] = 1.946022119085378e-01;
    gold_solution[5] = 1.981601232287249e-01;
    gold_solution[6] = 2.013936650249285e-01;
    gold_solution[7] = 2.043807296399895e-01;
    gold_solution[8] = 2.071701713934283e-01;
    gold_solution[9] = 2.097979282542038e-01;
    for (double time = 0.0; time <= charge_time+epsilon; time += time_step)
    {
        dev->evolve_one_time_step_constant_current(time_step, charge_current);
        dev->get_voltage(computed_voltage);
        if ((std::abs(time+time_step-1e-3) < 1e-7) || (std::abs(time+time_step-2e-3) < 1e-7) ||
            (std::abs(time+time_step-3e-3) < 1e-7) || (std::abs(time+time_step-4e-3) < 1e-7) ||
            (std::abs(time+time_step-5e-3) < 1e-7) || (std::abs(time+time_step-6e-3) < 1e-7) ||
            (std::abs(time+time_step-7e-3) < 1e-7) || (std::abs(time+time_step-8e-3) < 1e-7) ||
            (std::abs(time+time_step-9e-3) < 1e-7) || (std::abs(time+time_step-10e-3) < 1e-7))
        {
          BOOST_CHECK_CLOSE(computed_voltage, gold_solution[pos], percent_tolerance);
          ++pos;
        }
    }
}

} // end namespace cap

BOOST_AUTO_TEST_CASE( test_exact_transient_solution )
{
    // parse input file
    std::shared_ptr<boost::property_tree::ptree> input_database =
        std::make_shared<boost::property_tree::ptree>();
    boost::property_tree::info_parser::read_info("verification_problems.info", *input_database);

    // build an energy storage system
    std::shared_ptr<boost::property_tree::ptree> device_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("device"));
    device_database->put("type", "New_SuperCapacitor");
    std::shared_ptr<cap::EnergyStorageDevice> device =
        cap::EnergyStorageDevice::build(boost::mpi::communicator(), *device_database);

    // measure discharge curve
    std::fstream fout;
    fout.open("verification_problem_data", std::fstream::out);

    std::shared_ptr<boost::property_tree::ptree> verification_problem_database =
        std::make_shared<boost::property_tree::ptree>(input_database->get_child("verification_problem_subramanian"));

    cap::compute_parameters(device_database, verification_problem_database);

    cap::verification_problem(device, verification_problem_database);

    fout.close();
}    
