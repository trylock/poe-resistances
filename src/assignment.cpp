#include "assignment.hpp"

recap::assignment recap::find_assignment(
    resistance required, 
    const std::vector<recipe::slot_t>& slots, 
    const std::vector<recipe>& recipes)
{
    using recipe_index_t = std::uint8_t;
    // maximum number of slots
    constexpr std::size_t MAX_SLOT_COUNT = 16;

    // Check that we can fit all recipes into index type
    if (recipes.size() > std::numeric_limits<recipe_index_t>::max())
    {
        throw std::runtime_error{ "Recipes won't fit into used index type." };
    }

    // Check number of slots
    if (slots.size() > MAX_SLOT_COUNT)
    {
        throw std::runtime_error{ 
            "Internal type has memory only for " + 
            std::to_string(MAX_SLOT_COUNT) +  " slots." };
    }

    // Count number of distinct resistance values <= required
    const auto res_count = resistance{ 
        static_cast<resistance::item_t>(required.fire() + 1), 
        static_cast<resistance::item_t>(required.cold() + 1), 
        static_cast<resistance::item_t>(required.lightning() + 1), 
        static_cast<resistance::item_t>(required.chaos() + 1) 
    };
    const auto value_count = static_cast<std::size_t>(res_count.fire()) * res_count.cold() * 
        res_count.lightning() * res_count.chaos();

    // allocate memory for cost table and best assignment table 
    std::vector<recipe::cost_t> best_cost(value_count);
    std::fill(best_cost.begin(), best_cost.end(), recipe::MAX_COST);
    std::vector<std::array<recipe_index_t, MAX_SLOT_COUNT>> best_assignment(value_count);
    
    // allocate memory for the result so we can parallelize the computation
    auto next_best_cost = best_cost;
    auto next_best_assignment = best_assignment;

    // Convert resistance object to a linear index.
    // This is a one-to-one mapping from resistances < res_count to [0, value_count - 1]
    auto to_index = [res_count](resistance res)
    {
        std::size_t index = res.fire();
        index = index * res_count.cold() + res.cold();
        index = index * res_count.lightning() + res.lightning();
        index = index * res_count.chaos() + res.chaos();
        return index;
    };

    // we can always satisfy the requirement of 0 resistances
    best_cost[0] = 0;

    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        // initialize next cost with MAX_COST
        std::fill(next_best_cost.begin(), next_best_cost.end(), recipe::MAX_COST);

        // compute next best costs (with 1 more item)
        tbb::blocked_rangeNd<resistance::item_t, 4> range{ 
            tbb::blocked_range<resistance::item_t>{ 0, res_count.fire(), 1 },
            tbb::blocked_range<resistance::item_t>{ 0, res_count.cold(), 1 },
            tbb::blocked_range<resistance::item_t>{ 0, res_count.lightning(), 128 },
            tbb::blocked_range<resistance::item_t>{ 0, res_count.chaos(), 128 },
        };
        tbb::simple_partitioner partitioner;
        tbb::parallel_for(range, [&](auto&& local_range) 
        {
            // try all recipes for current resistance
            for (std::size_t recipe_index = 0; recipe_index < recipes.size(); ++recipe_index)
            {
                const auto& recipe = recipes[recipe_index];

                // if this recipe is not aplicable for slot i
                if ((recipe.slots() & slots[i]) == 0)
                {
                    continue; // skip this recipe
                }

                for (resistance::item_t fire = local_range.dim(0).begin(); fire != local_range.dim(0).end(); ++fire)
                {
                    for (resistance::item_t cold = local_range.dim(1).begin(); cold != local_range.dim(1).end(); ++cold)
                    {
                        for (resistance::item_t lightning = local_range.dim(2).begin(); lightning != local_range.dim(2).end(); ++lightning)
                        {
                            for (resistance::item_t chaos = local_range.dim(3).begin(); chaos != local_range.dim(3).end(); ++chaos)
                            {
                                // construct resistance object from the values and find its index
                                resistance current_resist{ fire, cold, lightning, chaos };
                                auto current_index = to_index(current_resist);
                                const auto& current_cost = next_best_cost[current_index];

                                // find required resistances if we use this recipe
                                resistance prev_resist = current_resist - recipe.resistances();
                                auto prev_index = to_index(prev_resist);
                                const auto& prev_cost = best_cost[prev_index];

                                // if this path is better
                                if (prev_cost + recipe.cost() < current_cost)
                                {
                                    // replace the recipe
                                    for (std::size_t j = 0; j < i; ++j)
                                    {
                                        next_best_assignment[current_index][j] = best_assignment[prev_index][j];
                                    }
                                    next_best_assignment[current_index][i] = static_cast<recipe_index_t>(recipe_index);

                                    // update the cost
                                    next_best_cost[current_index] = prev_cost + recipe.cost();
                                }
                            }
                        }
                    }
                }
            }
        }, partitioner);

        std::swap(next_best_cost, best_cost);
        std::swap(next_best_assignment, best_assignment);
    }

    // lookup the solution in the table
    auto result_index = to_index(required);
    auto result_cost = best_cost[result_index];
    auto result_assignment = best_assignment[result_index];
    
    // convert it to the output type
    assignment result;
    result.cost() = result_cost;

    if (result.cost() != recipe::MAX_COST)
    {
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            auto& used_recipe = recipes[result_assignment[i]];
            if (used_recipe.resistances() != resistance::make_zero())
            {
                result.assignments().push_back(recipe_assignment{ slots[i], used_recipe });
            }
        }
    }
    
    return result;
}