package app.organicmaps.routing;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.RecyclerView;
import app.organicmaps.R;
import app.organicmaps.sdk.routing.AlternativeRouteInfo;
import com.google.android.material.card.MaterialCardView;

public class AlternativeRoutesAdapter extends RecyclerView.Adapter<AlternativeRoutesAdapter.ViewHolder>
{
  public interface OnRouteSelectedListener
  {
    void onRouteSelected(int routeIndex);
  }

  @Nullable
  private AlternativeRouteInfo[] mRoutes;
  private int mSelectedIndex = 0;
  @Nullable
  private OnRouteSelectedListener mListener;
  @NonNull
  private final Context mContext;

  public AlternativeRoutesAdapter(@NonNull Context context)
  {
    mContext = context;
  }

  public void setRoutes(@NonNull AlternativeRouteInfo[] routes, int selectedIndex)
  {
    mRoutes = routes;
    mSelectedIndex = selectedIndex;
    notifyDataSetChanged();
  }

  public void setSelectedIndex(int index)
  {
    int oldIndex = mSelectedIndex;
    mSelectedIndex = index;
    notifyItemChanged(oldIndex);
    notifyItemChanged(index);
  }

  public void setOnRouteSelectedListener(OnRouteSelectedListener listener)
  {
    mListener = listener;
  }

  @NonNull
  @Override
  public ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType)
  {
    View view = LayoutInflater.from(parent.getContext())
        .inflate(R.layout.item_alternative_route, parent, false);
    return new ViewHolder(view);
  }

  @Override
  public void onBindViewHolder(@NonNull ViewHolder holder, int position)
  {
    AlternativeRouteInfo route = mRoutes[position];
    boolean isSelected = position == mSelectedIndex;

    // Route label
    holder.routeLabel.setText(mContext.getString(R.string.route_number, position + 1));

    // Time formatting (convert seconds to readable format)
    holder.routeTime.setText(formatDuration(route.durationSeconds));

    // Distance
    holder.routeDistance.setText(route.distance.toString(mContext));

    // Time difference (only for alternatives, index > 0)
    if (route.routeIndex > 0 && route.timeDifferenceSeconds != 0)
    {
      holder.routeTimeDiff.setVisibility(View.VISIBLE);
      String timeDiff = formatDuration(route.getAbsTimeDifference());
      if (route.isFaster())
      {
        holder.routeTimeDiff.setText(mContext.getString(R.string.route_faster, timeDiff));
        holder.routeTimeDiff.setTextColor(ContextCompat.getColor(mContext, R.color.routing_route_faster));
      }
      else
      {
        holder.routeTimeDiff.setText(mContext.getString(R.string.route_slower, timeDiff));
        holder.routeTimeDiff.setTextColor(ContextCompat.getColor(mContext, R.color.routing_route_slower));
      }
    }
    else
    {
      holder.routeTimeDiff.setVisibility(View.GONE);
    }

    // Selection state
    holder.card.setChecked(isSelected);
    holder.card.setStrokeWidth(isSelected ? 2 : 0);
    holder.card.setStrokeColor(ContextCompat.getColor(mContext, R.color.base_accent));

    holder.itemView.setOnClickListener(v -> {
      if (mListener != null)
        mListener.onRouteSelected(route.routeIndex);
    });
  }

  @Override
  public int getItemCount()
  {
    return mRoutes == null ? 0 : mRoutes.length;
  }

  private String formatDuration(int seconds)
  {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    if (hours > 0)
      return hours + mContext.getString(R.string.hour) + " " + minutes + mContext.getString(R.string.minute);
    return minutes + " " + mContext.getString(R.string.minute);
  }

  static class ViewHolder extends RecyclerView.ViewHolder
  {
    final MaterialCardView card;
    final TextView routeLabel;
    final TextView routeTime;
    final TextView routeDistance;
    final TextView routeTimeDiff;

    ViewHolder(@NonNull View itemView)
    {
      super(itemView);
      card = (MaterialCardView) itemView;
      routeLabel = itemView.findViewById(R.id.route_label);
      routeTime = itemView.findViewById(R.id.route_time);
      routeDistance = itemView.findViewById(R.id.route_distance);
      routeTimeDiff = itemView.findViewById(R.id.route_time_diff);
    }
  }
}
