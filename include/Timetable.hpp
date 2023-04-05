#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "Train.hpp"
#include "RailwayNetwork.hpp"
#include "Station.hpp"
#include <filesystem>

namespace cda_rail {
    struct ScheduledStop {
        /**
         * A scheduled stop.
         */
        int begin;
        int end;
        int station;

        bool operator<(const ScheduledStop& other) const {
            return (end < other.begin);
        }
        bool operator>(const ScheduledStop& other) const {
            return (begin > other.end);
        }
        bool operator==(const ScheduledStop& other) const {
            return (begin == other.begin && end == other.end);
        }
    };

    struct Schedule {
        /**
         * Schedule object
         * @param t_0 start time of schedule in seconds
         * @param v_0 initial velocity in m/s
         * @param entry entry vertex index of the schedule
         * @param t_n end time of schedule in seconds
         * @param v_n target end velocity in m/s
         * @param exit exit vertex index of the schedule
         * @param stops vector of scheduled stops
         *
         * For stops in stations he has to occupy the station for the entire interval.
         */
        int t_0;
        double v_0;
        int entry;
        int t_n;
        double v_n;
        int exit;
        std::vector<ScheduledStop> stops = {};
    };

    class Timetable {
        /**
         * Timetable class
         */
        private:
            cda_rail::StationList station_list;
            cda_rail::TrainList train_list;
            std::vector<Schedule> schedules;

            void set_train_list(const cda_rail::TrainList& tl);

        public:
            void add_train(const std::string& name, int length, double max_speed, double acceleration, double deceleration,
                           int t_0, double v_0, int entry, int t_n, double v_n, int exit, const cda_rail::Network& network);
            void add_train(const std::string& name, int length, double max_speed, double acceleration, double deceleration,
                           int t_0, double v_0, const std::string& entry, int t_n, double v_n, const std::string& exit,
                           const cda_rail::Network& network);

            void add_station(const std::string& name, const std::unordered_set<int>& tracks);
            void add_station(const std::string& name);
            void add_track_to_station(int station_index, int track, const cda_rail::Network& network);
            void add_track_to_station(const std::string& name, int track, const cda_rail::Network& network);
            void add_track_to_station(int station_index, int source, int target, const cda_rail::Network& network);
            void add_track_to_station(const std::string& name, int source, int target, const cda_rail::Network& network);
            void add_track_to_station(int station_index, const std::string& source, const std::string& target, const cda_rail::Network& network);
            void add_track_to_station(const std::string& name, const std::string& source, const std::string& target, const cda_rail::Network& network);

            void add_stop(int train_index, int station_index, int begin, int end, bool sort = true);
            void add_stop(const std::string& train_name, int station_index, int begin, int end, bool sort = true);
            void add_stop(int train_index, const std::string& station_name, int begin, int end, bool sort = true);
            void add_stop(const std::string& train_name, const std::string& station_name, int begin, int end, bool sort = true);

            [[nodiscard]] const cda_rail::StationList& get_station_list() const;
            [[nodiscard]] const cda_rail::TrainList& get_train_list() const;
            [[nodiscard]] const Schedule& get_schedule(int index) const;
            [[nodiscard]] const Schedule& get_schedule(const std::string& train_name) const;

            void sort_stops();

            void export_timetable(const std::string& path, const cda_rail::Network& network) const;
            void export_timetable(const char* path, const cda_rail::Network& network) const;
            void export_timetable(const std::filesystem::path& p, const cda_rail::Network& network) const;
            [[nodiscard]] static cda_rail::Timetable import_timetable(const std::string& path, const cda_rail::Network& network);
            [[nodiscard]] static cda_rail::Timetable import_timetable(const std::filesystem::path& p, const cda_rail::Network& network);
            [[nodiscard]] static cda_rail::Timetable import_timetable(const char* path, const cda_rail::Network& network);

    };
}