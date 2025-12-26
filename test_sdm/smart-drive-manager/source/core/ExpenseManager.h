// File Location: source/modules/ExpenseManager.h
// Smart Drive Manager - Expense & Budget Tracking

#ifndef EXPENSEMANAGER_H
#define EXPENSEMANAGER_H

#include "../../include/sdm_types.hpp"
#include "../../source/core/DatabaseManager.h"
#include "../../source/core/CacheManager.h"
#include "../../source/core/IndexManager.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <iostream>
using namespace std;


class ExpenseManager
{
private:
    DatabaseManager &db_;
    CacheManager &cache_;
    IndexManager &index_;

    struct BudgetLimit
    {
        uint64_t driver_id;
        ExpenseCategory category;
        double monthly_limit;
        double current_month_spent;
        uint64_t alert_threshold_percentage; 
    };

    vector<BudgetLimit> budget_limits_;

public:
    ExpenseManager(DatabaseManager &db, CacheManager &cache, IndexManager &index)
        : db_(db), cache_(cache), index_(index) {}


    uint64_t add_expense(uint64_t driver_id,
                         uint64_t vehicle_id,
                         ExpenseCategory category,
                         double amount,
                         const string &description,
                         uint64_t trip_id = 0)
    {
        uint64_t expense_id = generate_expense_id();

        ExpenseRecord expense;
        expense.expense_id = expense_id;
        expense.driver_id = driver_id;
        expense.vehicle_id = vehicle_id;
        expense.trip_id = trip_id;
        expense.category = category;
        expense.expense_date = get_current_timestamp();
        expense.amount = amount;
        strncpy(expense.currency, "USD", sizeof(expense.currency) - 1);
        strncpy(expense.description, description.c_str(), sizeof(expense.description) - 1);

        if (!db_.create_expense(expense))
        {
            return 0;
        }

        index_.insert_primary(4, expense_id, expense.expense_date, 0); 

        check_budget_alert(driver_id, category, amount);

        cache_.clear_query_cache();

        return expense_id;
    }

    uint64_t add_fuel_expense(uint64_t driver_id,
                              uint64_t vehicle_id,
                              uint64_t trip_id,
                              double fuel_quantity,
                              double price_per_unit,
                              const string &station)
    {
        uint64_t expense_id = generate_expense_id();

        ExpenseRecord expense;
        expense.expense_id = expense_id;
        expense.driver_id = driver_id;
        expense.vehicle_id = vehicle_id;
        expense.trip_id = trip_id;
        expense.category = ExpenseCategory::FUEL;
        expense.expense_date = get_current_timestamp();
        expense.fuel_quantity = fuel_quantity;
        expense.fuel_price_per_unit = price_per_unit;
        expense.amount = fuel_quantity * price_per_unit;
        strncpy(expense.currency, "USD", sizeof(expense.currency) - 1);
        strncpy(expense.fuel_station, station.c_str(), sizeof(expense.fuel_station) - 1);

        string desc = "Fuel: " + to_string(fuel_quantity) + "L at " + station;
        strncpy(expense.description, desc.c_str(), sizeof(expense.description) - 1);

        if (!db_.create_expense(expense))
        {
            return 0;
        }

        index_.insert_primary(4, expense_id, expense.expense_date, 0);
        check_budget_alert(driver_id, ExpenseCategory::FUEL, expense.amount);
        cache_.clear_query_cache();

        return expense_id;
    }

    vector<ExpenseRecord> get_driver_expenses(uint64_t driver_id, int limit = 100)
    {
        return db_.get_expenses_by_driver(driver_id, limit);
    }

    vector<ExpenseRecord> get_expenses_by_category(uint64_t driver_id,
                                                        ExpenseCategory category)
    {
        return db_.get_expenses_by_category(driver_id, category);
    }

    vector<ExpenseRecord> get_expenses_by_date_range(uint64_t driver_id,
                                                          uint64_t start_date,
                                                          uint64_t end_date)
    {
        auto all_expenses = db_.get_expenses_by_driver(driver_id, 100000);

        vector<ExpenseRecord> filtered;
        for (const auto &expense : all_expenses)
        {
            if (expense.expense_date >= start_date && expense.expense_date <= end_date)
            {
                filtered.push_back(expense);
            }
        }

        return filtered;
    }

    bool set_budget_limit(uint64_t driver_id,
                          ExpenseCategory category,
                          double monthly_limit,
                          uint64_t alert_percentage = 80)
    {
        // Find existing budget or create new
        auto it = find_if(budget_limits_.begin(), budget_limits_.end(),
                               [driver_id, category](const BudgetLimit &b)
                               {
                                   return b.driver_id == driver_id && b.category == category;
                               });

        if (it != budget_limits_.end())
        {
            it->monthly_limit = monthly_limit;
            it->alert_threshold_percentage = alert_percentage;
        }
        else
        {
            BudgetLimit budget;
            budget.driver_id = driver_id;
            budget.category = category;
            budget.monthly_limit = monthly_limit;
            budget.current_month_spent = 0;
            budget.alert_threshold_percentage = alert_percentage;
            budget_limits_.push_back(budget);
        }

        return true;
    }

    bool get_budget_status(uint64_t driver_id, ExpenseCategory category,
                           double &limit, double &spent, double &remaining)
    {
        auto it = find_if(budget_limits_.begin(), budget_limits_.end(),
                               [driver_id, category](const BudgetLimit &b)
                               {
                                   return b.driver_id == driver_id && b.category == category;
                               });

        if (it == budget_limits_.end())
        {
            return false;
        }

        uint64_t month_start = get_month_start_timestamp();
        uint64_t month_end = get_current_timestamp();

        auto expenses = get_expenses_by_date_range(driver_id, month_start, month_end);

        double total_spent = 0;
        for (const auto &expense : expenses)
        {
            if (expense.category == category)
            {
                total_spent += expense.amount;
            }
        }

        limit = it->monthly_limit;
        spent = total_spent;
        remaining = limit - spent;

        return true;
    }

    struct BudgetAlert
    {
        uint64_t driver_id;
        ExpenseCategory category;
        double limit;
        double spent;
        double percentage_used;
        bool over_budget;
    };

    vector<BudgetAlert> get_budget_alerts(uint64_t driver_id)
    {
        vector<BudgetAlert> alerts;

        for (const auto &budget : budget_limits_)
        {
            if (budget.driver_id != driver_id)
                continue;

            double limit, spent, remaining;
            if (get_budget_status(driver_id, budget.category, limit, spent, remaining))
            {
                double percentage = (spent / limit) * 100.0;

                if (percentage >= budget.alert_threshold_percentage || spent > limit)
                {
                    BudgetAlert alert;
                    alert.driver_id = driver_id;
                    alert.category = budget.category;
                    alert.limit = limit;
                    alert.spent = spent;
                    alert.percentage_used = percentage;
                    alert.over_budget = (spent > limit);
                    alerts.push_back(alert);
                }
            }
        }

        return alerts;
    }

    struct ExpenseSummary
    {
        double total_expenses;
        double fuel_expenses;
        double maintenance_expenses;
        double insurance_expenses;
        double toll_expenses;
        double parking_expenses;
        double other_expenses;

        map<ExpenseCategory, double> by_category;
        map<uint64_t, double> by_vehicle; // vehicle_id -> amount

        double average_daily_expense;
        double average_monthly_expense;
        int total_transactions;
    };

    ExpenseSummary get_expense_summary(uint64_t driver_id,
                                       uint64_t start_date,
                                       uint64_t end_date)
    {
        ExpenseSummary summary = {};

        auto expenses = get_expenses_by_date_range(driver_id, start_date, end_date);

        for (const auto &expense : expenses)
        {
            summary.total_expenses += expense.amount;
            summary.total_transactions++;

            // Categorize
            switch (expense.category)
            {
            case ExpenseCategory::FUEL:
                summary.fuel_expenses += expense.amount;
                break;
            case ExpenseCategory::MAINTENANCE:
                summary.maintenance_expenses += expense.amount;
                break;
            case ExpenseCategory::INSURANCE:
                summary.insurance_expenses += expense.amount;
                break;
            case ExpenseCategory::TOLL:
                summary.toll_expenses += expense.amount;
                break;
            case ExpenseCategory::PARKING:
                summary.parking_expenses += expense.amount;
                break;
            default:
                summary.other_expenses += expense.amount;
                break;
            }

            summary.by_category[expense.category] += expense.amount;

            summary.by_vehicle[expense.vehicle_id] += expense.amount;
        }

        uint64_t days = (end_date - start_date) / (86400ULL * 1000000000ULL);
        if (days > 0)
        {
            summary.average_daily_expense = summary.total_expenses / days;
            summary.average_monthly_expense = summary.total_expenses / (days / 30.0);
        }

        return summary;
    }

    struct MonthlyExpenseReport
    {
        uint32_t year;
        uint32_t month;
        double total;
        map<ExpenseCategory, double> by_category;
    };

    vector<MonthlyExpenseReport> get_monthly_reports(uint64_t driver_id,
                                                          int num_months = 12)
    {
        vector<MonthlyExpenseReport> reports;

        // Get current date components
        auto current = get_current_timestamp();

        for (int i = 0; i < num_months; i++)
        {
            MonthlyExpenseReport report;

            // Calculate month start/end (simplified)
            uint64_t month_start = current - (i * 30ULL * 86400ULL * 1000000000ULL);
            uint64_t month_end = month_start + (30ULL * 86400ULL * 1000000000ULL);

            auto expenses = get_expenses_by_date_range(driver_id, month_start, month_end);

            report.total = 0;
            for (const auto &expense : expenses)
            {
                report.total += expense.amount;
                report.by_category[expense.category] += expense.amount;
            }

            reports.push_back(report);
        }

        reverse(reports.begin(), reports.end());
        return reports;
    }

    

    struct TaxReport
    {
        double total_deductible_expenses;
        double total_non_deductible;
        vector<ExpenseRecord> deductible_expenses;
        uint64_t start_date;
        uint64_t end_date;
    };

    TaxReport generate_tax_report(uint64_t driver_id,
                                  uint64_t start_date,
                                  uint64_t end_date)
    {
        TaxReport report;
        report.start_date = start_date;
        report.end_date = end_date;

        auto expenses = get_expenses_by_date_range(driver_id, start_date, end_date);

        for (const auto &expense : expenses)
        {
            if (expense.is_tax_deductible)
            {
                report.total_deductible_expenses += expense.amount;
                report.deductible_expenses.push_back(expense);
            }
            else
            {
                report.total_non_deductible += expense.amount;
            }
        }

        return report;
    }

    bool mark_expense_tax_deductible(uint64_t expense_id, bool deductible,
                                     double tax_amount = 0)
    {
        // Would need to update expense record
        // Simplified implementation
        return true;
    }

    struct CostPerKilometer
    {
        double total_distance;
        double total_cost;
        double cost_per_km;
        double fuel_cost_per_km;
        double maintenance_cost_per_km;
        double other_cost_per_km;
    };

    CostPerKilometer calculate_cost_per_km(uint64_t driver_id,
                                           uint64_t start_date,
                                           uint64_t end_date)
    {
        CostPerKilometer result = {};

        auto expenses = get_expenses_by_date_range(driver_id, start_date, end_date);

        double total_distance = 0;

        double fuel_cost = 0, maintenance_cost = 0, other_cost = 0;

        for (const auto &expense : expenses)
        {
            result.total_cost += expense.amount;

            switch (expense.category)
            {
            case ExpenseCategory::FUEL:
                fuel_cost += expense.amount;
                break;
            case ExpenseCategory::MAINTENANCE:
                maintenance_cost += expense.amount;
                break;
            default:
                other_cost += expense.amount;
                break;
            }
        }

        if (total_distance > 0)
        {
            result.total_distance = total_distance;
            result.cost_per_km = result.total_cost / total_distance;
            result.fuel_cost_per_km = fuel_cost / total_distance;
            result.maintenance_cost_per_km = maintenance_cost / total_distance;
            result.other_cost_per_km = other_cost / total_distance;
        }

        return result;
    }

private:
    uint64_t generate_expense_id()
    {
        static uint64_t counter = 1;
        return counter++;
    }

    uint64_t get_current_timestamp()
    {
        return chrono::system_clock::now().time_since_epoch().count();
    }

    uint64_t get_month_start_timestamp()
    {
        auto now = chrono::system_clock::now();
        auto time_t = chrono::system_clock::to_time_t(now);
        auto tm = *localtime(&time_t);
        tm.tm_mday = 1;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;

        auto month_start = chrono::system_clock::from_time_t(mktime(&tm));
        return month_start.time_since_epoch().count();
    }

    void check_budget_alert(uint64_t driver_id, ExpenseCategory category, double amount)
    {
        auto it = find_if(budget_limits_.begin(), budget_limits_.end(),
                               [driver_id, category](const BudgetLimit &b)
                               {
                                   return b.driver_id == driver_id && b.category == category;
                               });

        if (it != budget_limits_.end())
        {
            it->current_month_spent += amount;
            
            double percentage = (it->current_month_spent / it->monthly_limit) * 100.0;

            if (percentage >= it->alert_threshold_percentage || it->current_month_spent > it->monthly_limit)
            {
                cout << "\n💰 BUDGET ALERT!" << endl;
                cout << "Category: " << get_category_name(category) << endl;
                cout << "Spent: $" << fixed << setprecision(2) 
                         << it->current_month_spent << " / $" << it->monthly_limit << endl;
                cout << "Usage: " << percentage << "%" << endl;
                
                if (it->current_month_spent > it->monthly_limit) {
                    cout << "⚠️  OVER BUDGET!" << endl;
                }
                cout << endl;
            }
        }
    }

    string get_category_name(ExpenseCategory category)
    {
        switch (category)
        {
        case ExpenseCategory::FUEL:
            return "Fuel";
        case ExpenseCategory::MAINTENANCE:
            return "Maintenance";
        case ExpenseCategory::INSURANCE:
            return "Insurance";
        case ExpenseCategory::TOLL:
            return "Toll";
        case ExpenseCategory::PARKING:
            return "Parking";
        default:
            return "Other";
        }
    }
};

#endif // EXPENSEMANAGER_H