#include "CustomExceptions.hpp"
#include "Definitions.hpp"
#include "nlohmann/json.hpp"
#include "probleminstances/VSSGenerationTimetable.hpp"
#include "solver/mip-based/VSSGenTimetableSolver.hpp"

#include <cmath>
#include <fstream>
#include <unordered_set>
#include <vector>

using json = nlohmann::json;

// NOLINTBEGIN(performance-inefficient-string-concatenation)

cda_rail::instances::SolVSSGenerationTimetable::SolVSSGenerationTimetable(
    cda_rail::instances::VSSGenerationTimetable instance, int dt)
    : instance(std::move(instance)), dt(dt) {
  this->initialize_vectors();
}

double
cda_rail::instances::SolVSSGenerationTimetable::get_train_pos(size_t train_id,
                                                              int time) const {
  if (!instance.get_train_list().has_train(train_id)) {
    throw exceptions::TrainNotExistentException(train_id);
  }

  const auto& [t0, tn] = instance.time_index_interval(train_id, dt, true);
  if (t0 * dt > time || tn * dt < time) {
    throw exceptions::ConsistencyException("Train " + std::to_string(train_id) +
                                           " is not scheduled at time " +
                                           std::to_string(time));
  }

  if (time % dt == 0) {
    const auto t_index = static_cast<size_t>(time / dt) - t0;
    return train_pos.at(train_id).at(t_index);
  }

  const auto t_1 = static_cast<size_t>(std::floor(time / dt)) - t0;
  const auto t_2 = t_1 + 1;

  const auto x_1 = train_pos.at(train_id).at(t_1);
  const auto v_1 = train_speed.at(train_id).at(t_1);
  const auto x_2 = train_pos.at(train_id).at(t_2);
  const auto v_2 = train_speed.at(train_id).at(t_2);

  if (approx_equal(x_2 - x_1, 0.5 * dt * (v_1 + v_2))) {
    const auto a   = (v_2 - v_1) / dt;
    const auto tau = time - static_cast<double>(t0 + t_1) * dt;
    return x_1 + v_1 * tau + 0.5 * a * tau * tau;
  }

  throw exceptions::ConsistencyException(
      "Train " + std::to_string(train_id) + " is not scheduled at time " +
      std::to_string(time) + " and cannot be inferred by linear interpolation");
}

double cda_rail::instances::SolVSSGenerationTimetable::get_train_speed(
    size_t train_id, int time) const {
  if (!instance.get_train_list().has_train(train_id)) {
    throw exceptions::TrainNotExistentException(train_id);
  }

  const auto& [t0, tn] = instance.time_index_interval(train_id, dt, true);
  if (t0 * dt > time || tn * dt < time) {
    throw exceptions::ConsistencyException("Train " + std::to_string(train_id) +
                                           " is not scheduled at time " +
                                           std::to_string(time));
  }

  if (time % dt == 0) {
    const auto t_index = static_cast<size_t>(time / dt) - t0;
    return train_speed.at(train_id).at(t_index);
  }

  const auto t_1 = static_cast<size_t>(std::floor(time / dt)) - t0;
  const auto t_2 = t_1 + 1;

  const auto x_1 = train_pos.at(train_id).at(t_1);
  const auto v_1 = train_speed.at(train_id).at(t_1);
  const auto x_2 = train_pos.at(train_id).at(t_2);
  const auto v_2 = train_speed.at(train_id).at(t_2);

  if (approx_equal(x_2 - x_1, 0.5 * dt * (v_1 + v_2))) {
    const auto a   = (v_2 - v_1) / dt;
    const auto tau = time - static_cast<double>(t0 + t_1) * dt;
    return v_1 + a * tau;
  }

  throw exceptions::ConsistencyException(
      "Train " + std::to_string(train_id) + " is not scheduled at time " +
      std::to_string(time) + " and cannot be inferred by linear interpolation");
}

void cda_rail::instances::SolVSSGenerationTimetable::add_vss_pos(
    size_t edge_id, double pos, bool reverse_edge) {
  if (!instance.const_n().has_edge(edge_id)) {
    throw exceptions::EdgeNotExistentException(edge_id);
  }

  const auto& edge = instance.const_n().get_edge(edge_id);

  if (pos <= 0 || pos >= edge.length) {
    throw exceptions::ConsistencyException(
        "VSS position " + std::to_string(pos) + " is not on edge " +
        std::to_string(edge_id));
  }

  vss_pos.at(edge_id).emplace_back(pos);
  std::sort(vss_pos.at(edge_id).begin(), vss_pos.at(edge_id).end());

  if (reverse_edge) {
    const auto reverse_edge_index =
        instance.const_n().get_reverse_edge_index(edge_id);
    if (reverse_edge_index.has_value()) {
      vss_pos.at(reverse_edge_index.value()).emplace_back(edge.length - pos);
      std::sort(vss_pos.at(reverse_edge_index.value()).begin(),
                vss_pos.at(reverse_edge_index.value()).end());
    }
  }
}

void cda_rail::instances::SolVSSGenerationTimetable::set_vss_pos(
    size_t edge_id, std::vector<double> pos) {
  if (!instance.const_n().has_edge(edge_id)) {
    throw exceptions::EdgeNotExistentException(edge_id);
  }

  const auto& edge = instance.const_n().get_edge(edge_id);

  for (const auto& p : pos) {
    if (p < 0 || p > edge.length) {
      throw exceptions::ConsistencyException(
          "VSS position " + std::to_string(p) + " is not on edge " +
          std::to_string(edge_id));
    }
  }

  vss_pos.at(edge_id) = std::move(pos);
}

void cda_rail::instances::SolVSSGenerationTimetable::reset_vss_pos(
    size_t edge_id) {
  if (!instance.const_n().has_edge(edge_id)) {
    throw exceptions::EdgeNotExistentException(edge_id);
  }

  vss_pos.at(edge_id).clear();
}

void cda_rail::instances::SolVSSGenerationTimetable::add_train_pos(
    size_t train_id, int time, double pos) {
  if (pos < 0) {
    throw exceptions::ConsistencyException(
        "Train position " + std::to_string(pos) + " is negative");
  }

  if (!instance.get_train_list().has_train(train_id)) {
    throw exceptions::TrainNotExistentException(train_id);
  }

  const auto& [t0, tn] = instance.time_index_interval(train_id, dt, true);
  if (t0 * dt > time || tn * dt < time) {
    throw exceptions::ConsistencyException("Train " + std::to_string(train_id) +
                                           " is not scheduled at time " +
                                           std::to_string(time));
  }

  if (time % dt != 0) {
    throw exceptions::ConsistencyException(
        "Time " + std::to_string(time) +
        " is not a multiple of dt = " + std::to_string(dt));
  }

  const auto tr_t0   = instance.time_index_interval(train_id, dt, true).first;
  const auto t_index = static_cast<size_t>(time / dt) - tr_t0;
  train_pos.at(train_id).at(t_index) = pos;
}

void cda_rail::instances::SolVSSGenerationTimetable::add_train_speed(
    size_t train_id, int time, double speed) {
  if (!instance.get_train_list().has_train(train_id)) {
    throw exceptions::TrainNotExistentException(train_id);
  }

  if (speed < 0) {
    throw exceptions::ConsistencyException(
        "Train speed " + std::to_string(speed) + " is negative");
  }
  if (speed > instance.get_train_list().get_train(train_id).max_speed) {
    throw exceptions::ConsistencyException(
        "Train speed " + std::to_string(speed) +
        " is greater than the maximum speed of train " +
        std::to_string(train_id) + " (" +
        std::to_string(
            instance.get_train_list().get_train(train_id).max_speed) +
        ")");
  }

  const auto& [t0, tn] = instance.time_index_interval(train_id, dt, true);
  if (t0 * dt > time || tn * dt < time) {
    throw exceptions::ConsistencyException("Train " + std::to_string(train_id) +
                                           " is not scheduled at time " +
                                           std::to_string(time));
  }

  if (time % dt != 0) {
    throw exceptions::ConsistencyException(
        "Time " + std::to_string(time) +
        " is not a multiple of dt = " + std::to_string(dt));
  }

  const auto tr_t0   = instance.time_index_interval(train_id, dt, true).first;
  const auto t_index = static_cast<size_t>(time / dt) - tr_t0;
  train_speed.at(train_id).at(t_index) = speed;
}

bool cda_rail::instances::SolVSSGenerationTimetable::check_consistency() const {
  if (status == SolutionStatus::Unknown) {
    return false;
  }
  if (obj < 0) {
    return false;
  }
  if (dt < 0) {
    return false;
  }
  if (!instance.check_consistency(true)) {
    return false;
  }
  for (const auto& train_pos_vec : train_pos) {
    for (const auto& pos : train_pos_vec) {
      if (pos < 0) {
        return false;
      }
    }
  }
  for (size_t tr_id = 0; tr_id < train_speed.size(); ++tr_id) {
    const auto& train = instance.get_train_list().get_train(tr_id);
    for (double t : train_speed.at(tr_id)) {
      if (t < 0 || t > train.max_speed) {
        return false;
      }
    }
  }
  for (size_t edge_id = 0; edge_id < vss_pos.size(); ++edge_id) {
    const auto& edge = instance.const_n().get_edge(edge_id);
    for (const auto& pos : vss_pos.at(edge_id)) {
      if (pos < 0 || pos > edge.length) {
        return false;
      }
    }
  }
  return true;
}

void cda_rail::instances::SolVSSGenerationTimetable::export_solution(
    const std::filesystem::path& p, bool export_instance) const {
  /**
   * This method exports the solution object to a specific path. This includes
   * the following:
   * - If export_instance is true, the instance is exported to the path p /
   * instance
   * - If export_instance is false, the routes are exported to the path p /
   * instance / routes
   * - dt, status, obj, and postprocessed are exported to p / solution /
   * data.json
   * - vss_pos is exported to p / solution / vss_pos.json
   * - train_pos and train_speed are exported to p / solution / train_pos.json
   * and p / solution / train_speed.json The method throws a
   * ConsistencyException if the solution is not consistent.
   *
   * @param p the path to the folder where the solution should be exported
   * @param export_instance whether the instance should be exported next to the
   * solution
   */

  if (!check_consistency()) {
    throw exceptions::ConsistencyException();
  }

  if (!is_directory_and_create(p / "solution")) {
    throw exceptions::ExportException("Could not create directory " +
                                      p.string());
  }

  if (export_instance) {
    instance.export_instance(p / "instance");
  } else {
    instance.routes.export_routes(p / "instance" / "routes",
                                  instance.const_n());
  }

  json data;
  data["dt"]            = dt;
  data["status"]        = static_cast<int>(status);
  data["obj"]           = obj;
  data["mip_obj"]       = mip_obj;
  data["postprocessed"] = postprocessed;
  std::ofstream data_file(p / "solution" / "data.json");
  data_file << data << std::endl;
  data_file.close();

  json vss_pos_json;
  for (size_t edge_id = 0; edge_id < instance.const_n().number_of_edges();
       ++edge_id) {
    const auto& edge = instance.const_n().get_edge(edge_id);
    const auto& v0   = instance.const_n().get_vertex(edge.source).name;
    const auto& v1   = instance.const_n().get_vertex(edge.target).name;
    vss_pos_json["('" + v0 + "', '" + v1 + "')"] = vss_pos.at(edge_id);
  }

  std::ofstream vss_pos_file(p / "solution" / "vss_pos.json");
  vss_pos_file << vss_pos_json << std::endl;
  vss_pos_file.close();

  json train_pos_json;
  json train_speed_json;
  for (size_t tr_id = 0; tr_id < instance.get_train_list().size(); ++tr_id) {
    const auto& train       = instance.get_train_list().get_train(tr_id);
    const auto  tr_interval = instance.time_index_interval(tr_id, dt, true);
    json        train_pos_json_tmp;
    json        train_speed_json_tmp;
    for (size_t t_id = 0; t_id < train_pos.at(tr_id).size(); ++t_id) {
      const auto t = static_cast<int>(tr_interval.first + t_id) * dt;
      train_pos_json_tmp[std::to_string(t)]   = train_pos.at(tr_id).at(t_id);
      train_speed_json_tmp[std::to_string(t)] = train_speed.at(tr_id).at(t_id);
    }
    train_pos_json[train.name]   = train_pos_json_tmp;
    train_speed_json[train.name] = train_speed_json_tmp;
  }

  std::ofstream train_pos_file(p / "solution" / "train_pos.json");
  train_pos_file << train_pos_json << std::endl;
  train_pos_file.close();

  std::ofstream train_speed_file(p / "solution" / "train_speed.json");
  train_speed_file << train_speed_json << std::endl;
  train_speed_file.close();
}

cda_rail::instances::SolVSSGenerationTimetable::SolVSSGenerationTimetable(
    const std::filesystem::path&                 p,
    const std::optional<VSSGenerationTimetable>& instance) {
  if (!std::filesystem::exists(p)) {
    throw exceptions::ImportException("Path does not exist");
  }
  if (!std::filesystem::is_directory(p)) {
    throw exceptions::ImportException("Path is not a directory");
  }

  bool const import_routes = instance.has_value();
  if (instance.has_value()) {
    this->instance = instance.value();
  } else {
    this->instance = VSSGenerationTimetable(p / "instance");
  }

  if (import_routes) {
    this->instance.routes =
        RouteMap(p / "instance" / "routes", this->instance.const_n());
  }

  if (!this->instance.check_consistency(true)) {
    throw exceptions::ConsistencyException(
        "Imported instance is not consistent");
  }

  // Read data
  std::ifstream data_file(p / "solution" / "data.json");
  json          data  = json::parse(data_file);
  this->dt            = data["dt"].get<int>();
  this->status        = static_cast<SolutionStatus>(data["status"].get<int>());
  this->obj           = data["obj"].get<double>();
  this->mip_obj       = data["mip_obj"].get<double>();
  this->postprocessed = data["postprocessed"].get<bool>();

  this->initialize_vectors();

  // Read vss_pos
  std::ifstream vss_pos_file(p / "solution" / "vss_pos.json");
  json          vss_pos_json = json::parse(vss_pos_file);
  for (const auto& [key, val] : vss_pos_json.items()) {
    std::string source_name;
    std::string target_name;
    extract_vertices_from_key(key, source_name, target_name);
    const auto vss_pos_vector = val.get<std::vector<double>>();
    set_vss_pos(source_name, target_name, vss_pos_vector);
  }

  // Read train_pos
  std::ifstream train_pos_file(p / "solution" / "train_pos.json");
  json          train_pos_json = json::parse(train_pos_file);
  for (const auto& [tr_name, tr_pos_json] : train_pos_json.items()) {
    for (const auto& [t, pos] : tr_pos_json.items()) {
      this->add_train_pos(tr_name, std::stoi(t), pos.get<double>());
    }
  }

  // Read train_speed
  std::ifstream train_speed_file(p / "solution" / "train_speed.json");
  json          train_speed_json = json::parse(train_speed_file);
  for (const auto& [tr_name, tr_speed_json] : train_speed_json.items()) {
    for (const auto& [t, speed] : tr_speed_json.items()) {
      this->add_train_speed(tr_name, std::stoi(t), speed.get<double>());
    }
  }

  if (!this->check_consistency()) {
    throw exceptions::ConsistencyException(
        "Imported solution object is not consistent");
  }
}

void cda_rail::instances::SolVSSGenerationTimetable::initialize_vectors() {
  vss_pos = std::vector<std::vector<double>>(
      this->instance.const_n().number_of_edges());
  train_pos.reserve(this->instance.get_train_list().size());
  train_speed.reserve(this->instance.get_train_list().size());

  for (size_t tr = 0; tr < this->instance.get_train_list().size(); ++tr) {
    const auto tr_interval = this->instance.time_index_interval(tr, dt, true);
    const auto tr_interval_size = tr_interval.second - tr_interval.first + 1;
    train_pos.emplace_back(tr_interval_size, -1);
    train_speed.emplace_back(tr_interval_size, -1);
  }
}

cda_rail::instances::SolVSSGenerationTimetable
cda_rail::solver::mip_based::VSSGenTimetableSolver::extract_solution(
    bool postprocess, bool debug, ExportOption export_option,
    const std::string&                                      name,
    const std::optional<instances::VSSGenerationTimetable>& old_instance)
    const {
  if (debug) {
    std::cout << "Extracting solution object..." << std::endl;
  }

  auto sol_obj = instances::SolVSSGenerationTimetable(
      (old_instance.has_value() ? old_instance.value() : instance), dt);

  const auto grb_status = model->get(GRB_IntAttr_Status);

  if (grb_status == GRB_OPTIMAL) {
    std::cout << "Solution status: Optimal" << std::endl;
    sol_obj.set_status(SolutionStatus::Optimal);
  } else if (grb_status == GRB_INFEASIBLE) {
    std::cout << "Solution status: Infeasible" << std::endl;
    sol_obj.set_status(SolutionStatus::Infeasible);
  } else if (grb_status == GRB_TIME_LIMIT &&
             model->get(GRB_IntAttr_SolCount) >= 1) {
    std::cout << "Solution status: Feasible (optimality unknown)" << std::endl;
    sol_obj.set_status(SolutionStatus::Feasible);
  } else if (grb_status == GRB_TIME_LIMIT &&
             model->get(GRB_IntAttr_SolCount) == 0) {
    std::cout << "Solution status: Timeout (Feasibility unknown)" << std::endl;
    sol_obj.set_status(SolutionStatus::Timeout);
  } else {
    throw exceptions::ConsistencyException(
        "Gurobi status code " + std::to_string(grb_status) + " unknown.");
  }

  if (const auto sol_count = model->get(GRB_IntAttr_SolCount); sol_count <= 1) {
    return sol_obj;
  }

  const auto mip_obj_val =
      static_cast<int>(std::round(model->get(GRB_DoubleAttr_ObjVal)));
  sol_obj.set_mip_obj(mip_obj_val);
  if (debug) {
    std::cout << "MIP objective: " << mip_obj_val << std::endl;
  }

  if (vss_model.get_model_type() == vss::ModelType::Discrete) {
    // TODO: Implement
    return sol_obj;
  }

  int obj = 0;

  for (size_t r_e_index = 0; r_e_index < relevant_edges.size(); ++r_e_index) {
    const auto  e_index      = relevant_edges.at(r_e_index);
    const auto  vss_number_e = instance.const_n().max_vss_on_edge(e_index);
    const auto& e            = instance.const_n().get_edge(e_index);
    const auto  reverse_edge_index =
        instance.const_n().get_reverse_edge_index(e_index);
    for (size_t vss = 0; vss < vss_number_e; ++vss) {
      bool b_used = false;

      if (vss_model.get_model_type() == vss::ModelType::Continuous) {
        b_used =
            vars.at("b_used").at(r_e_index, vss).get(GRB_DoubleAttr_X) > 0.5;
      } else if (vss_model.get_model_type() == vss::ModelType::Inferred) {
        b_used =
            vars.at("num_vss_segments").at(r_e_index).get(GRB_DoubleAttr_X) >
            static_cast<double>(vss) + 1.5;
      }

      if (postprocess && b_used) {
        if (debug) {
          const auto& source = instance.const_n().get_vertex(e.source).name;
          const auto& target = instance.const_n().get_vertex(e.target).name;
          std::cout << "Postprocessing on " << source << " to " << target
                    << std::endl;
        }
        b_used = false;
        for (size_t tr = 0; tr < num_tr; ++tr) {
          for (size_t t = train_interval.at(tr).first;
               t <= train_interval.at(tr).second; ++t) {
            const auto front1 =
                vars.at("b_front")
                    .at(tr, t, breakable_edge_indices.at(e_index), vss)
                    .get(GRB_DoubleAttr_X) > 0.5;
            const auto rear1 =
                vars.at("b_rear")
                    .at(tr, t, breakable_edge_indices.at(e_index), vss)
                    .get(GRB_DoubleAttr_X) > 0.5;
            const auto front2 =
                reverse_edge_index.has_value()
                    ? vars.at("b_front")
                              .at(tr, t,
                                  breakable_edge_indices.at(
                                      reverse_edge_index.value()),
                                  vss)
                              .get(GRB_DoubleAttr_X) > 0.5
                    : false;
            const auto rear2 =
                reverse_edge_index.has_value()
                    ? vars.at("b_rear")
                              .at(tr, t,
                                  breakable_edge_indices.at(
                                      reverse_edge_index.value()),
                                  vss)
                              .get(GRB_DoubleAttr_X) > 0.5
                    : false;
            if (front1 || rear1 || front2 || rear2) {
              b_used = true;
              break;
            }
          }
          if (b_used) {
            break;
          }
        }
      }

      if (!b_used) {
        continue;
      }

      const auto b_pos_val = vars.at("b_pos")
                                 .at(breakable_edge_indices.at(e_index), vss)
                                 .get(GRB_DoubleAttr_X);
      if (debug) {
        const auto& source = instance.const_n().get_vertex(e.source).name;
        const auto& target = instance.const_n().get_vertex(e.target).name;
        std::cout << "Add VSS at " << b_pos_val << " on " << source << " to "
                  << target << std::endl;
      }
      sol_obj.add_vss_pos(e_index, b_pos_val, true);
      obj += 1;
    }
  }

  sol_obj.set_obj(obj);

  if (!fix_routes) {
    sol_obj.reset_routes();
    if (debug) {
      std::cout << "Extracting routes" << std::endl;
    }
    for (size_t tr = 0; tr < num_tr; ++tr) {
      const auto train = instance.get_train_list().get_train(tr);
      sol_obj.add_empty_route(train.name);
      size_t current_vertex = instance.get_schedule(tr).entry;
      for (size_t t = train_interval[tr].first; t <= train_interval[tr].second;
           ++t) {
        std::unordered_set<size_t> edge_list;
        for (int e = 0; e < num_edges; ++e) {
          const auto tr_on_edge =
              vars.at("x").at(tr, t, e).get(GRB_DoubleAttr_X) > 0.5;
          if (tr_on_edge && edge_list.count(e) == 0) {
            edge_list.emplace(e);
          }
        }
        bool edge_added = true;
        while (!edge_list.empty() && edge_added) {
          edge_added = false;
          for (const auto& e : edge_list) {
            if (instance.const_n().get_edge(e).source == current_vertex) {
              sol_obj.push_back_edge_to_route(train.name, e);
              current_vertex = instance.const_n().get_edge(e).target;
              edge_list.erase(e);
              edge_added = true;
              break;
            }
          }
        }
      }
    }
  }

  for (size_t tr = 0; tr < num_tr; ++tr) {
    const auto train = instance.get_train_list().get_train(tr);
    for (size_t t = train_interval[tr].first;
         t <= train_interval[tr].second + 1; ++t) {
      const auto train_speed_val = vars.at("v").at(tr, t).get(GRB_DoubleAttr_X);
      sol_obj.add_train_speed(tr, static_cast<int>(t) * dt, train_speed_val);
    }
  }

  for (size_t tr = 0; tr < num_tr; ++tr) {
    const auto  train  = instance.get_train_list().get_train(tr);
    const auto& tr_len = train.length;
    for (auto t = train_interval[tr].first; t <= train_interval[tr].second;
         ++t) {
      double train_pos = -1;
      if (fix_routes) {
        train_pos = vars.at("lda").at(tr, t).get(GRB_DoubleAttr_X) + tr_len;
      } else {
        // TODO: Free Routes
      }
      sol_obj.add_train_pos(tr, t * dt, train_pos);
    }

    auto   t         = train_interval[tr].second + 1;
    double train_pos = -1;
    if (fix_routes) {
      train_pos = vars.at("mu").at(tr, t - 1).get(GRB_DoubleAttr_X);
    } else {
      // TODO: Free Routes
    }
    if (include_braking_curves) {
      train_pos -= vars.at("brakelen").at(tr, t - 1).get(GRB_DoubleAttr_X);
    }
    sol_obj.add_train_pos(tr, t * dt, train_pos);
  }

  if (export_option == ExportOption::ExportSolution ||
      export_option == ExportOption::ExportSolutionWithInstance ||
      export_option == ExportOption::ExportSolutionAndLP ||
      export_option == ExportOption::ExportSolutionWithInstanceAndLP) {
    const bool export_instance =
        (export_option == ExportOption::ExportSolutionWithInstance ||
         export_option == ExportOption::ExportSolutionWithInstanceAndLP);
    sol_obj.export_solution(name, export_instance);
  }

  return sol_obj;
}

// NOLINTEND(performance-inefficient-string-concatenation)
